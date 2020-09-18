//
//  File: %t-bitset.c
//  Summary: "bitset datatype"
//  Section: datatypes
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
// See %sys-bitset.h for explanation of methodology.


#include "sys-core.h"

#ifdef __cplusplus
extern "C" {
#endif

extern bool roaring_realloc_array(roaring_array_t *ra, int32_t new_capacity);
extern void roaring_size_updated(roaring_array_t *ra);
extern void roaring_flags_updated(roaring_array_t *ra);

#ifdef __cplusplus
}
#endif

// This lets us sync the REBSER with updates to the size.
//
bool roaring_realloc_array(roaring_array_t *ra, int32_t desired_capacity) {
    //
    // !!! This may fail if out of memory; can that leave a bitset corrupt?
    // We'd have to trap the error and return false :-/
    //
    REBBIT *bits = SER(ra->hookdata);

    REBSER *proxy = Make_Bitset(desired_capacity);
    int32_t capacity = SER_REST(proxy);
    assert(capacity >= desired_capacity);

    void **newcontainers = cast(void**, SER_DATA(proxy));
    uint16_t *newkeys = cast(uint16_t *, newcontainers + capacity);
    uint8_t *newtypecodes = cast(uint8_t *, newkeys + capacity);
    if(ra->size > 0) {
      memcpy(newcontainers, ra->containers, sizeof(void *) * ra->size);
      memcpy(newkeys, ra->keys, sizeof(uint16_t) * ra->size);
      memcpy(newtypecodes, ra->typecodes, sizeof(uint8_t) * ra->size);
    }

    proxy->misc.inverted = bits->misc.inverted;
    Swap_Series_Content(proxy, bits);
    SET_SERIES_USED(proxy, 0);  // don't want it freeing pointed-tos
    Free_Unmanaged_Series(proxy);  // would free containers if used > 0

    // In case the data was in the node itself (singular) assign these fields
    // *after* the swap.
    //
    SET_SERIES_USED(bits, ra->size);
    ra->containers = cast(void**, SER_DATA(bits));
    ra->keys = cast(uint16_t*, ra->containers + capacity);
    ra->typecodes = cast(uint8_t*, ra->keys + capacity);
    ra->allocation_size = capacity;

    return true;
}

void roaring_size_updated(roaring_array_t *ra)
  { SET_SERIES_USED(SER(ra->hookdata), ra->size); }

void roaring_flags_updated(roaring_array_t *ra)
  { mutable_FOURTH_BYTE(SER(ra->hookdata)->leader.bits) = ra->flags; }


//
//  CT_Bitset: C
//
// !!! R3-Alpha comparison is strange, it's different in stackless branch:
// https://forum.rebol.info/t/comparison-semantics/1318
//
REBINT CT_Bitset(REBCEL(const*) a, REBCEL(const*) b, bool strict)
{
    // !!! Comparing case-insensitively is a weird idea for bitsets, as they
    // may-or-may-not represent characters.  It has been suggested that
    // CHARSET! be a distinct type.  This needs review.
    //
    UNUSED(strict);

    const REBBIT *bits_a = VAL_BITSET(a);
    const REBBIT *bits_b = VAL_BITSET(b);

    // Because bitsets effectively stretch out to "infinity", a negated
    // bitset can never be equal to a non-negated one...a negated bitset
    // has infinite values set at the end, and a plain one has infinite
    // unset values at the end.
    //
    if (bits_a->misc.inverted != bits_b->misc.inverted)
        return bits_a->misc.inverted ? 1 : -1;

    roaring_bitmap_t r_a;
    roaring_bitmap_t r_b;
    Roaring_From_Bitset(&r_a, bits_a);
    Roaring_From_Bitset(&r_b, bits_b);

    uint64_t card1 = roaring_bitmap_get_cardinality(&r_a);
    uint64_t card2 = roaring_bitmap_get_cardinality(&r_b);
    if (card1 != card2)
        return card1 > card2 ? 1 : -1;

    if (roaring_bitmap_equals(&r_a, &r_b))
        return 0;

    // !!! How to order bitsets that have the same cardinality but are not
    // equal?  Addresses are a bad choice.
    //
    return CELL_TO_VAL(a) > CELL_TO_VAL(b) ? 1 : -1;
}


//
//  Make_Bitset: C
//
// !!! Note that the `capacity` is the number of internal compression
// container units.  This isn't something the average user is going to have
// any idea how to estimate...but it roughly corresponds to the entropy
// level of the data (how hard it is to compress).
//
REBBIT *Make_Bitset(REBLEN desired_capacity)
{
    REBSER *s = Make_Series(desired_capacity, FLAG_FLAVOR(BITSET));
    s->misc.inverted = false;
    return s;
}


void Mold_Uint32_t(REB_MOLD *mo, uint32_t value) {
    REBYTE buf[60];
    REBINT len = Emit_Integer(buf, value);
    Append_Ascii_Len(mo->series, s_cast(buf), len);
    Append_Codepoint(mo->series, ' ');
}


//
//  MF_Bitset: C
//
void MF_Bitset(REB_MOLD *mo, REBCEL(const*) v, bool form)
{
    UNUSED(form); // all bitsets are "molded" at this time

    Pre_Mold(mo, v); // #[bitset! or make bitset!

    const REBBIT *bits = VAL_BITSET(v);
    roaring_bitmap_t r;
    Roaring_From_Bitset(&r, bits);

    // Just show a limited number of items in the set.

    REBLEN num_printed = 0;

    roaring_uint32_iterator_t iter;
    roaring_init_iterator(&r, &iter);

    Append_Codepoint(mo->series, '[');

    if (bits->misc.inverted)
        Append_Ascii(mo->series, "not ");

    while (iter.has_value) {
        Mold_Uint32_t(mo, iter.current_value);
        roaring_advance_uint32_iterator(&iter);
        ++num_printed;
        if (num_printed > 24) {
            Append_Ascii(mo->series, "...");
            break;
        }
    }

    REBYTE *last = STR_TAIL(mo->series) - 1;
    if (*last == ' ')
        *last = ']';
    else
        Append_Codepoint(mo->series, ']');

    Append_Ascii(mo->series, " ra->size=");
    Mold_Uint32_t(mo, r.high_low_container.size);
    Append_Ascii(mo->series, " ra->allocation_size=");
    Mold_Uint32_t(mo, r.high_low_container.allocation_size);

    End_Mold(mo);
}


//
//  MAKE_Bitset: C
//
REB_R MAKE_Bitset(
    REBVAL *out,
    enum Reb_Kind kind,
    option(const REBVAL*) parent,
    const REBVAL *arg
){
    assert(kind == REB_BITSET);
    if (parent)
        fail (Error_Bad_Make_Parent(kind, unwrap(parent)));

    REBLEN desired_capacity;
    if (IS_INTEGER(arg))
        desired_capacity = VAL_UINT32(arg);
    else
        desired_capacity = 0;
    if (desired_capacity > 1024)
        fail ("MAKE BITSET! takes a container/entropy count, keep it small");

    REBBIT *bits = Make_Bitset(desired_capacity);
    Init_Bitset(out, bits);

    if (IS_BINARY(arg)) {  // operates with the "BITS #{...}" dialect logic
        Update_Bitset_Bits(bits, 0, VAL_BINARY(arg), VAL_INDEX(arg));
        return out;
    }

    if (
        IS_BLOCK(arg)
        and IS_WORD(VAL_ARRAY_AT(nullptr, arg))
        and VAL_WORD_ID(VAL_ARRAY_AT(nullptr, arg)) == SYM__NOT_
    ){
        // Special: the bitset is actually inverted from what we're saying
        // !!! Should this just be `negate make bitset! []` ?
        //
        DECLARE_LOCAL (arg_next);
        Copy_Cell(arg_next, arg);
        ++VAL_INDEX_RAW(arg_next);
        Update_Bitset(bits, arg_next, true);

        Negate_Bitset(bits);
    }
    else
        Update_Bitset(bits, arg, true);

    return out;
}


//
//  TO_Bitset: C
//
REB_R TO_Bitset(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    return MAKE_Bitset(out, kind, nullptr, arg);
}


//
//  Bitset_Contains_Core: C
//
// Check bit indicated. Returns true if set.
// If uncased is true, try to match either upper or lower case.
//
bool Bitset_Contains_Core(const REBBIT *bits, REBLEN n, bool uncased)
{
    roaring_bitmap_t r;
    Roaring_From_Bitset(&r, bits);

    bool b;
    if (not uncased or n >= UNICODE_CASES)
        b = roaring_bitmap_contains(&r, n);
    else {
        if (roaring_bitmap_contains(&r, LO_CASE(n)))
            b = true;
        else if (roaring_bitmap_contains(&r, UP_CASE(n)))
            b = true;
        else
            b = false;
    }
    return bits->misc.inverted ? not b : b;
}


//
//  Update_Bitset_Core: C
//
// Set/clear a single bit. Expand if needed.
//
void Update_Bitset_Core(REBBIT *bits, REBLEN n, bool set)
{
    if (n > MAX_BITSET)
        fail ("Value is outside of bitset bounds");

    roaring_bitmap_t r;
    Roaring_From_Bitset(&r, bits);

    bool add = bits->misc.inverted ? not set : set;
    if (add)
        roaring_bitmap_add(&r, n);
    else
        roaring_bitmap_remove(&r, n);
}


//
//  Update_Bitset_Bits: C
//
// Treat a BINARY! as bits to apply over a range.
//
void Update_Bitset_Bits(
    REBBIT *bits,
    REBLEN start,
    const REBBIN *bin,
    REBLEN at
){
    roaring_bitmap_t r;
    Roaring_From_Bitset(&r, bits);

    REBLEN n = start;

    const REBYTE *bp = BIN_AT(bin, at);
    const REBYTE *ep = BIN_TAIL(bin);

    for (; bp != ep; ++bp) {
        REBYTE bit = 0x80;
        for (; bit != 0; ++n, bit = bit >> 1) {
            bool set = *bp & bit;
            bool add = bits->misc.inverted ? not set : set;
            if (add)
                roaring_bitmap_add(&r, n);
            else
                roaring_bitmap_remove(&r, n);
        }
    }

    Optimize_Bitset(bits);
}


//
//  Update_Bitset: C
//
// Set/clear bits indicated by strings and chars and ranges.
//
void Update_Bitset(REBBIT *bits, const RELVAL *val, bool set)
{
    if (IS_INTEGER(val)) {
        Update_Bitset_Core(bits, Int32s(val, 0), set);
        return;
    }

    if (IS_BINARY(val)) {
        //
        // BINARY! hitorically sets each byte value.  If the binary is to be
        // interpreted as its component bits, see BITS keyword in BLOCK!
        // dialect interpretation below.
        //
        REBLEN i = VAL_INDEX(val);
        const REBYTE *bp = BIN_HEAD(VAL_BINARY(val));
        for (; i != VAL_LEN_HEAD(val); i++)
            Update_Bitset_Core(bits, bp[i], set);
        return;
    }

    if (IS_ISSUE(val) or ANY_STRING(val)) {
        REBLEN len;
        REBCHR(const*) up = VAL_UTF8_LEN_SIZE_AT(&len, nullptr, val);
        for (; len > 0; --len) {
            REBUNI c;
            up = NEXT_CHR(&c, up);
            Update_Bitset_Core(bits, c, set);
        }
        return;
    }

    if (not ANY_ARRAY(val))
        fail (Error_Invalid_Type(VAL_TYPE(val)));

    const RELVAL *tail;
    const RELVAL *item = VAL_ARRAY_AT(&tail, val);
    if (item == tail)
        return;

    // !!! Syntax of historical bitsets is that NOT at the beginning meant the
    // rest of the descriptions were all inverted.
    //
    if (IS_WORD(item) and VAL_WORD_ID(item) == SYM__NOT_) {
        set = not set;
        Negate_Bitset(bits);
        ++item;
    }

    // Loop through block of bit specs.
    //
    // !!! How extensive should this dialect be?  What should TAG!, ISSUE!,
    // URL!, FILE!, or ISSUE! etc. do?
    //
    // !!! Handling of hyphens is repeated and inelegant.  Review as well.

    for (; item != tail; item++) {
        switch (VAL_TYPE(item)) {
        case REB_ISSUE: {
            if (not IS_CHAR(item)) {  // no special handling for hyphen
                Update_Bitset(bits, SPECIFIC(item), set);
                break;
            }
            REBUNI c = VAL_CHAR(item);
            if (
                item + 1 != tail
                && IS_WORD(item + 1)
                && VAL_WORD_ID(item + 1) == SYM_HYPHEN
            ){
                item += 2;
                if (IS_CHAR(item)) {
                    REBLEN n = VAL_CHAR(item);
                    if (n < c)
                        fail (Error_Index_Out_Of_Range_Raw());
                    do {
                        Update_Bitset_Core(bits, c, set);
                    } while (c++ < n); // post-increment: test before overflow
                }
                else
                    fail (Error_Bad_Value_Core(item, VAL_SPECIFIER(val)));
            }
            else
                Update_Bitset_Core(bits, c, set);
            break; }

        case REB_INTEGER: {
            REBLEN n = Int32s(item, 0);
            if (n > MAX_BITSET)
                fail ("INTEGER! is greater than maximum value for BITSET!");
            if (
                item + 1 != tail
                && IS_WORD(item + 1)
                && VAL_WORD_ID(item + 1) == SYM_HYPHEN
            ){
                REBUNI c = n;
                item += 2;
                if (IS_INTEGER(item)) {
                    n = Int32s(item, 0);
                    if (n < c)
                        fail (Error_Index_Out_Of_Range_Raw());
                    for (; c <= n; c++)
                        Update_Bitset_Core(bits, c, set);
                }
                else
                    fail (Error_Bad_Value_Core(item, VAL_SPECIFIER(val)));
            }
            else
                Update_Bitset_Core(bits, n, set);
            break; }

        case REB_BINARY:  // see BITS for special dialect handling
        case REB_TEXT:
            Update_Bitset(bits, SPECIFIC(item), set);
            break;

        case REB_WORD: {
            // Special: BITS #{000...}
            if (VAL_WORD_ID(item) != SYM_BITS) {
                PROBE(item);
                fail (Error_Bad_Value_Core(item, VAL_SPECIFIER(val)));
            }
            item++;
            if (not IS_BINARY(item))
                fail ("BITS in BITSET! dialect only works with BINARY!");

            // There's actually a bitmap container type, we could just make
            // one and use the merging lower level code to handle that.
            //
            fail ("TBD: add binary one bit at a time"); }

        default:
            fail ("Invalid item in BITSET! spec block");
        }
    }
}


//
//  Bitset_Contains: C
//
// Check bits indicated by strings and chars and ranges.
// If uncased is true, try to match either upper or lower case.
//
bool Bitset_Contains(const REBBIT *bits, const RELVAL *val, bool uncased)
{
    if (IS_CHAR(val))
        return Bitset_Contains_Core(bits, VAL_CHAR(val), uncased);

    if (IS_INTEGER(val))
        return Bitset_Contains_Core(bits, Int32s(val, 0), uncased);

    if (IS_BINARY(val)) {
        REBLEN i = VAL_INDEX(val);
        const REBYTE *bp = BIN_HEAD(VAL_BINARY(val));
        for (; i != VAL_LEN_HEAD(val); ++i)
            if (Bitset_Contains_Core(bits, bp[i], uncased))
                return true;
        return false;
    }

    if (ANY_STRING(val)) {
        REBLEN len;
        REBCHR(const*) up = VAL_UTF8_LEN_SIZE_AT(&len, nullptr, val);
        for (; len > 0; --len) {
            REBUNI c;
            up = NEXT_CHR(&c, up);
            if (Bitset_Contains_Core(bits, c, uncased))
                return true;
        }

        return false;
    }

    if (not ANY_ARRAY(val))
        fail (Error_Invalid_Type(VAL_TYPE(val)));

    // Loop through block of bit specs

    const RELVAL *tail;
    const RELVAL *item = VAL_ARRAY_AT(&tail, val);
    for (; item != tail; item++) {

        switch (VAL_TYPE(item)) {
        case REB_ISSUE: {
            if (not IS_CHAR(item))
                return Bitset_Contains(bits, SPECIFIC(item), uncased);

            REBUNI c = VAL_CHAR(item);
            if (IS_WORD(item + 1) && VAL_WORD_ID(item + 1) == SYM_HYPHEN) {
                item += 2;
                if (IS_CHAR(item)) {
                    REBLEN n = VAL_CHAR(item);
                    if (n < c)
                        fail (Error_Index_Out_Of_Range_Raw());
                    for (; c <= n; c++)
                        if (Bitset_Contains_Core(bits, c, uncased))
                            return true;
                }
                else
                    fail (Error_Bad_Value_Core(item, VAL_SPECIFIER(val)));
            }
            else
                if (Bitset_Contains_Core(bits, c, uncased))
                    return true;
            break; }

          case REB_INTEGER: {
            REBLEN n = Int32s(item, 0);
            if (IS_WORD(item + 1) && VAL_WORD_ID(item + 1) == SYM_HYPHEN) {
                REBUNI c = n;
                item += 2;
                if (IS_INTEGER(item)) {
                    n = Int32s(item, 0);
                    if (n < c)
                        fail (Error_Index_Out_Of_Range_Raw());
                    for (; c <= n; c++)
                        if (Bitset_Contains_Core(bits, c, uncased))
                            return true;
                }
                else
                    fail (Error_Bad_Value_Core(item, VAL_SPECIFIER(val)));
            }
            else
                if (Bitset_Contains_Core(bits, n, uncased))
                    return true;
            break; }

          case REB_BINARY:
          case REB_TEXT:
            if (Bitset_Contains(bits, SPECIFIC(item), uncased))
                return true;
            break;

        // !!! No support for BITS keyword in checking dialect?

          default:
            fail (Error_Invalid_Type(VAL_TYPE(item)));
        }
    }

    return false;
}


//
//  PD_Bitset: C
//
REB_R PD_Bitset(
    REBPVS *pvs,
    const RELVAL *picker,
    option(const REBVAL*) setval
){
    if (not setval) {
        if (Bitset_Contains(VAL_BITSET(pvs->out), picker, false))
            return Init_True(pvs->out);
        return nullptr; // !!! Red false on out of range, R3-Alpha NONE! (?)
    }

    REBBIT *bits = VAL_BITSET_ENSURE_MUTABLE(pvs->out);
    Update_Bitset(bits, picker, IS_TRUTHY(unwrap(setval)));
    return R_INVISIBLE;
}


//
//  REBTYPE: C
//
REBTYPE(Bitset)
{
    REBVAL *v = D_ARG(1);

    SYMID sym = VAL_WORD_ID(verb);
    switch (sym) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value)); // covered by `v`

        roaring_bitmap_t r;
        Roaring_From_Bitset(&r, VAL_BITSET(v));

        SYMID property = VAL_WORD_ID(ARG(property));
        switch (property) {
          case SYM_LENGTH:
            return Init_Integer(D_OUT, roaring_bitmap_get_cardinality(&r));

          case SYM_TAIL_Q:
            // Necessary to make EMPTY? work:
            return Init_Logic(D_OUT, roaring_bitmap_is_empty(&r));

          default:
            break;
        }

        break; }

    // Add AND, OR, XOR

      case SYM_FIND: {
        INCLUDE_PARAMS_OF_FIND;
        UNUSED(PAR(series));  // covered by `v`

        UNUSED(REF(reverse));  // Deprecated https://forum.rebol.info/t/1126
        UNUSED(REF(last));  // ...a HIJACK in %mezz-legacy errors if used

        if (REF(part) or REF(skip) or REF(tail) or REF(match))
            fail (Error_Bad_Refines_Raw());

        if (not Bitset_Contains(VAL_BITSET(v), ARG(pattern), did REF(case)))
            return nullptr;
        return Init_True(D_OUT); }

      case SYM_COMPLEMENT: {
        roaring_bitmap_t r;
        Roaring_From_Bitset(&r, VAL_BITSET_ENSURE_MUTABLE(v));

        Negate_Bitset(VAL_BITSET_ENSURE_MUTABLE(v));
        RETURN (v); }

      case SYM_APPEND:  // Accepts: #"a" "abc" [1 - 10] [#"a" - #"z"] etc.
      case SYM_INSERT: {
        REBVAL *arg = D_ARG(2);
        if (IS_NULLED_OR_BLANK(arg))
            RETURN (v);  // don't fail on read only if it would be a no-op

        Update_Bitset(VAL_BITSET_ENSURE_MUTABLE(v), arg, true);

        RETURN (v); }

      case SYM_REMOVE: {
        INCLUDE_PARAMS_OF_REMOVE;
        UNUSED(PAR(series));  // covered by `v`

        if (not REF(part))
            fail (Error_Missing_Arg_Raw());

        Update_Bitset(VAL_BITSET_ENSURE_MUTABLE(v), ARG(part), false);

        RETURN (v); }

      case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;
        UNUSED(PAR(value));

        if (REF(part) or REF(deep) or REF(types))
            fail (Error_Bad_Refines_Raw());

        const REBBIT *bits = VAL_BITSET(v);
        REBLEN capacity = SER_REST(m_cast(REBBIT*, bits));  // !!! temp ugly

        roaring_bitmap_t r;
        Roaring_From_Bitset(&r, bits);

        REBBIT *copy = Make_Bitset(capacity);
        roaring_bitmap_t r_copy;
        Roaring_From_Bitset(&r_copy, copy);

        roaring_bitmap_overwrite(&r_copy, &r);
        copy->misc.inverted = bits->misc.inverted;

        return Init_Bitset(D_OUT, copy); }

      case SYM_CLEAR: {
        REBBIT *bits = VAL_BITSET_ENSURE_MUTABLE(v);
        roaring_bitmap_t r;
        Roaring_From_Bitset(&r, bits);

        roaring_bitmap_clear(&r);
        bits->misc.inverted = false;

        RETURN (v); }

    // !!! Note: The below changes fix #2365

      case SYM_UNIQUE:
        RETURN (v);  // bitsets unique by definition

      case SYM_INTERSECT:
      case SYM_UNION:
      case SYM_DIFFERENCE:
      case SYM_EXCLUDE: {
        REBVAL *arg = D_ARG(2);
        if (not IS_BITSET(arg))
            fail (Error_Math_Args(VAL_TYPE(arg), verb));

        REBBIT *bits = VAL_BITSET_ENSURE_MUTABLE(v);
        roaring_bitmap_t r;
        Roaring_From_Bitset(&r, bits);

        const REBBIT *bits_arg = VAL_BITSET(arg);
        roaring_bitmap_t r_arg;
        Roaring_From_Bitset(&r_arg, bits_arg);

        // The inversion state of the result depends on how the out of bounds
        // states need to be treated.  This is a function of which operation
        // is used (e.g. an INTERSECT will only return a negated result if
        // both sets were negated, because that's the only way out-of-bounds
        // elements can result as true, while UNION returns a negated result
        // if either input was negated).
        //
        // But for the set operation itself, the sense of "truth" must be
        // consistent in order to use AND for INTERSECT, etc.  To keep the
        // code short for now, both sets are un-negated prior to the operation
        // and then the result is switched based on what is appropriate for
        // the operation given the incoming states.

        uint64_t flip_max = 0xDECAFBAD;  // init to avoid warning
        if (bits->misc.inverted or bits_arg->misc.inverted) {
            if (
                roaring_bitmap_is_empty(&r)
                and roaring_bitmap_is_empty(&r_arg)
            ){
                // roaring_bitmap_maximum() lies on empty sets and returns 0
                // as the max value, even though 0 is a legitimate value to
                // be in the set (and it isn't).  Handle this case specially.
                //
                flip_max = 0;  // non-inclusive bound
            }
            else {
                uint32_t max_r_arg = roaring_bitmap_maximum(&r_arg);
                uint32_t max_r = roaring_bitmap_maximum(&r);
                flip_max = MAX(max_r, max_r_arg) + 1;  // non-inclusive bound
                if (bits->misc.inverted)
                    roaring_bitmap_flip_inplace(&r, 0, flip_max);
                if (bits_arg->misc.inverted)  // !!! should copy, not mutate
                    roaring_bitmap_flip_inplace(&r_arg, 0, flip_max);
            }
        }

        // Now that they are adjusted to have the same sense of "set", use the
        // appropriate operation, and adjust the output so that its negated
        // sense matches the needed out-of-range response.
        //
        if (sym == SYM_UNION) {
            roaring_bitmap_or_inplace(&r, &r_arg);

            // Result needs to be negated if either input was negated
            //
            if (bits->misc.inverted or bits_arg->misc.inverted) {
                roaring_bitmap_flip_inplace(&r, 0, flip_max);
                bits->misc.inverted = true;
            }
            else
                assert(bits->misc.inverted == false);  // leave as is
        }
        else if (sym == SYM_INTERSECT) {
            roaring_bitmap_and_inplace(&r, &r_arg);

            // Result needs to be negated if both inputs were negated
            //
            if (bits->misc.inverted and bits_arg->misc.inverted) {
                roaring_bitmap_flip_inplace(&r, 0, flip_max);
                bits->misc.inverted = true;
            }
            else
                bits->misc.inverted = false;
        }
        else if (sym == SYM_DIFFERENCE) {
            roaring_bitmap_xor_inplace(&r, &r_arg);

            // Result is inverted if only one of the inputs was negated
            //
            if (bits->misc.inverted != bits_arg->misc.inverted) {
                roaring_bitmap_flip_inplace(&r, 0, flip_max);
                bits->misc.inverted = true;
            }
            else
                bits->misc.inverted = false;
        }
        else {
            assert(sym == SYM_EXCLUDE);

            roaring_bitmap_andnot_inplace(&r, &r_arg);

            // Result is inverted if r is inverted but r_arg is not inverted
            // (so as to cancel out its out-of-range-true elements).
            //
            if (bits->misc.inverted and not bits_arg->misc.inverted) {
                roaring_bitmap_flip_inplace(&r, 0, flip_max);
                assert(bits->misc.inverted);
            }
            else
                bits->misc.inverted = false;
        }

        if (bits_arg->misc.inverted) {  // !!! put arg back (fix, use copy!)
            roaring_bitmap_flip_inplace(&r_arg, 0, flip_max);
            Optimize_Bitset(m_cast(REBBIT*, bits_arg));
        }

        Optimize_Bitset(bits);

        RETURN (v); }

      default:
        break;
    }

    return R_UNHANDLED;
}
