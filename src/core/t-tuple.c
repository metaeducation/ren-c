//
//  File: %t-tuple.c
//  Summary: "tuple datatype"
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

#include "sys-core.h"


//
//  MAKE_Sequence: C
//
// !!! There was no original TO TUPLE! code besides calling this MAKE, so
// PATH!'s TO ANY-PATH! was used for TO ANY-TUPLE!.  But this contains some
// unique behavior which might be interesting for numeric MAKEs.
//
REB_R MAKE_Sequence(
    REBVAL *out,
    enum Reb_Kind kind,
    const REBVAL *opt_parent,
    const REBVAL *arg
){
    if (kind == REB_TEXT or ANY_PATH_KIND(kind))  // delegate for now
        return MAKE_Path(out, kind, opt_parent, arg);

    assert(kind == REB_TUPLE);
    if (opt_parent)
        fail (Error_Bad_Make_Parent(kind, opt_parent));

    if (IS_TUPLE(arg))
        return Move_Value(out, arg);

    // !!! Net lookup parses IP addresses out of `tcp://93.184.216.34` or
    // similar URL!s.  In Rebol3 these captures come back the same type
    // as the input instead of as STRING!, which was a latent bug in the
    // network code of the 12-Dec-2012 release:
    //
    // https://github.com/rebol/rebol/blob/master/src/mezz/sys-ports.r#L110
    //
    // All attempts to convert a URL!-flavored IP address failed.  Taking
    // URL! here fixes it, though there are still open questions.
    //
    if (IS_URL(arg)) {
        REBSIZ len;
        const REBYTE *cp
            = Analyze_String_For_Scan(&len, arg, MAX_SCAN_TUPLE);

        if (len == 0)
            fail (arg);

        const REBYTE *ep;
        REBLEN size = 1;
        REBINT n;
        for (n = cast(REBINT, len), ep = cp; n > 0; n--, ep++) { // count '.'
            if (*ep == '.')
                ++size;
        }

        if (size > MAX_TUPLE)
            fail (arg);

        if (size < 3)
            size = 3;

        REBYTE buf[MAX_TUPLE];

        REBYTE *tp = buf;
        for (ep = cp; len > cast(REBLEN, ep - cp); ++ep) {
            ep = Grab_Int(ep, &n);
            if (n < 0 || n > 255)
                fail (arg);

            *tp++ = cast(REBYTE, n);
            if (*ep != '.')
                break;
        }

        if (len > cast(REBLEN, ep - cp))
            fail (arg);

        return Init_Tuple_Bytes(out, buf, size);
    }

    if (ANY_ARRAY(arg)) {
        REBLEN len = 0;
        REBINT n;

        const RELVAL *item = VAL_ARRAY_AT(arg);

        REBYTE buf[MAX_TUPLE];
        REBYTE *vp = buf;

        for (; NOT_END(item); ++item, ++vp, ++len) {
            if (len >= MAX_TUPLE)
                goto bad_make;
            if (IS_INTEGER(item)) {
                n = Int32(item);
            }
            else if (IS_CHAR(item)) {
                n = VAL_CHAR(item);
            }
            else
                goto bad_make;

            if (n > 255 || n < 0)
                goto bad_make;
            *vp = n;
        }

        return Init_Tuple_Bytes(out, buf, len);
    }

    REBLEN alen;

    if (IS_ISSUE(arg)) {
        REBYTE buf[MAX_TUPLE];
        REBYTE *vp = buf;

        const REBSTR *spelling = VAL_STRING(arg);
        const REBYTE *ap = STR_HEAD(spelling);
        size_t size = STR_SIZE(spelling); // UTF-8 len
        if (size & 1)
            fail (arg); // must have even # of chars
        size /= 2;
        if (size > MAX_TUPLE)
            fail (arg); // valid even for UTF-8
        for (alen = 0; alen < size; alen++) {
            REBYTE decoded;
            if ((ap = Scan_Hex2(&decoded, ap)) == NULL)
                fail (arg);
            *vp++ = decoded;
        }
        Init_Tuple_Bytes(out, buf, size);
    }
    else if (IS_BINARY(arg)) {
        REBLEN len = VAL_LEN_AT(arg);
        if (len > MAX_TUPLE)
            len = MAX_TUPLE;
        Init_Tuple_Bytes(out, VAL_BIN_AT(arg), len);
    }
    else
        fail (arg);

    return out;

  bad_make:
    fail (Error_Bad_Make(REB_TUPLE, arg));
}


//
//  REBTYPE: C
//
// !!! This is shared code between TUPLE! and PATH!.  The math operations
// predate the unification, and are here to document what expected operations
// were...though they should use the method of PAIR! to generate frames for
// each operation and run them against each other.
//
REBTYPE(Sequence)
{
    REBVAL *sequence = D_ARG(1);

    // !!! We get bytes for the sequence even if it's not a legitimate byte
    // tuple (or path), for compatibility in the below code for when it is.
    // This is a work in progress, just to try to get to booting.
    //
    REBYTE buf[MAX_TUPLE];
    REBLEN len = VAL_SEQUENCE_LEN(sequence);
    bool all_byte_sized_ints = Did_Get_Sequence_Bytes(buf, sequence, len);
    UNUSED(all_byte_sized_ints);
    REBYTE *vp = buf;

    REBSYM sym = VAL_WORD_SYM(verb);

    // !!! This used to depend on "IS_BINARY_ACT", a concept that does not
    // exist any longer with symbol-based action dispatch.  Patch with more
    // elegant mechanism.
    //
    if (
        sym == SYM_ADD
        or sym == SYM_SUBTRACT
        or sym == SYM_MULTIPLY
        or sym == SYM_DIVIDE
        or sym == SYM_REMAINDER
        or sym == SYM_INTERSECT
        or sym == SYM_UNION
        or sym == SYM_DIFFERENCE
    ){
        assert(vp);

        REBYTE abuf[MAX_TUPLE];
        const REBYTE *ap;
        REBLEN alen;
        REBINT a;
        REBDEC dec;

        REBVAL *arg = D_ARG(2);

        if (IS_INTEGER(arg)) {
            dec = -207.6382; // unused but avoid maybe uninitialized warning
            a = VAL_INT32(arg);
            ap = nullptr;
        }
        else if (IS_DECIMAL(arg) || IS_PERCENT(arg)) {
            dec = VAL_DECIMAL(arg);
            a = cast(REBINT, dec);
            ap = nullptr;
        }
        else if (IS_TUPLE(arg)) {
            dec = -251.8517; // unused but avoid maybe uninitialized warning
            alen = VAL_SEQUENCE_LEN(arg);
            Get_Tuple_Bytes(abuf, arg, alen);
            ap = abuf;
            if (len < alen)
                len = alen;
            a = 646699; // unused but avoid maybe uninitialized warning
        }
        else
            fail (Error_Math_Args(REB_TUPLE, verb));

        REBLEN temp = len;
        for (; temp > 0; --temp, ++vp) {
            REBINT v = *vp;
            if (ap)
                a = (REBINT) *ap++;

            switch (VAL_WORD_SYM(verb)) {
            case SYM_ADD: v += a; break;

            case SYM_SUBTRACT: v -= a; break;

            case SYM_MULTIPLY:
                if (IS_DECIMAL(arg) || IS_PERCENT(arg))
                    v = cast(REBINT, v * dec);
                else
                    v *= a;
                break;

            case SYM_DIVIDE:
                if (IS_DECIMAL(arg) || IS_PERCENT(arg)) {
                    if (dec == 0.0)
                        fail (Error_Zero_Divide_Raw());

                    // !!! After moving all the ROUND service routines to
                    // talk directly to ROUND frames, cases like this that
                    // don't have round frames need one.  Can't run:
                    //
                    //    v = cast(REBINT, Round_Dec(v / dec, 0, 1.0));
                    //
                    // The easiest way to do it is to call ROUND.  Methods for
                    // this are being improved all the time, so the slowness
                    // of scanning and binding is not too important.  (The
                    // TUPLE! code is all going to be replaced... so just
                    // consider this an API test.)
                    //
                    v = rebUnboxInteger(
                        "to integer! round divide", rebI(v), arg,
                    rebEND);
                }
                else {
                    if (a == 0)
                        fail (Error_Zero_Divide_Raw());
                    v /= a;
                }
                break;

            case SYM_REMAINDER:
                if (a == 0)
                    fail (Error_Zero_Divide_Raw());
                v %= a;
                break;

            case SYM_INTERSECT:
                v &= a;
                break;

            case SYM_UNION:
                v |= a;
                break;

            case SYM_DIFFERENCE:
                v ^= a;
                break;

            default:
                return R_UNHANDLED;
            }

            if (v > 255)
                v = 255;
            else if (v < 0)
                v = 0;
            *vp = cast(REBYTE, v);
        }
        return Init_Tuple_Bytes(D_OUT, buf, len);
    }

    // !!!! merge with SWITCH below !!!
    if (sym == SYM_COMPLEMENT) {
        REBLEN temp = len;
        for (; temp > 0; --temp, vp++)
            *vp = cast(REBYTE, ~*vp);
        return Init_Tuple_Bytes(D_OUT, buf, len);
    }
    if (sym == SYM_RANDOM) {
        INCLUDE_PARAMS_OF_RANDOM;

        UNUSED(PAR(value));

        if (REF(only))
            fail (Error_Bad_Refines_Raw());

        if (REF(seed))
            fail (Error_Bad_Refines_Raw());
        for (; len > 0; len--, vp++) {
            if (*vp)
                *vp = cast(REBYTE, Random_Int(did REF(secure)) % (1 + *vp));
        }
        return Init_Tuple_Bytes(D_OUT, buf, len);
    }

    switch (sym) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value));

        switch (VAL_WORD_SYM(ARG(property))) {
          case SYM_LENGTH:
            return Init_Integer(D_OUT, VAL_SEQUENCE_LEN(sequence));

          case SYM_INDEX:  // Note: not legal, paths always at head, no index
          default:
            break;
        }
        break; }

        // ANY-SEQUENCE! is immutable, so a shallow copy should be a no-op,
        // but it should be cheap for any similarly marked array.  Also, a
        // /DEEP copy of a path may copy groups that are mutable.
        //
      case SYM_COPY: {
        if (
            MIRROR_BYTE(sequence) == REB_WORD
            or MIRROR_BYTE(sequence) == REB_CHAR
        ){
            assert(VAL_WORD_SYM(sequence) == SYM__SLASH_1_);
            return Move_Value(frame_->out, sequence);
        }

        assert(MIRROR_BYTE(sequence) == REB_BLOCK);

        enum Reb_Kind kind = VAL_TYPE(sequence);
        mutable_KIND_BYTE(sequence) = REB_BLOCK;

        REB_R r = T_Array(frame_, verb);

        assert(KIND_BYTE(r) == REB_BLOCK);
        Freeze_Array_Shallow(VAL_ARRAY_KNOWN_MUTABLE(r));
        mutable_KIND_BYTE(r) = kind;

        return r; }

      case SYM_REVERSE: {
        INCLUDE_PARAMS_OF_REVERSE;

        UNUSED(PAR(series));

        REBLEN temp = len;

        if (REF(part)) {
            REBLEN part = Get_Num_From_Arg(ARG(part));
            temp = MIN(part, VAL_SEQUENCE_LEN(sequence));
        }
        if (len > 0) {
            REBLEN i;
            for (i = 0; i < temp/2; i++) {
                REBINT a = vp[temp - i - 1];
                vp[temp - i - 1] = vp[i];
                vp[i] = a;
            }
        }
        return Init_Tuple_Bytes(D_OUT, buf, len); }

      default:
        break;
    }

    return R_UNHANDLED;
}


//
//  PD_Sequence: C
//
// Shared code for picking/setting items out of PATH!s and TUPLE!s.
// Note that compressed storage choices for these immutable types means they
// may not be implemented underneath as arrays.
//
REB_R PD_Sequence(
    REBPVS *pvs,
    const RELVAL *picker,
    const REBVAL *opt_setval
){
    if (opt_setval)
        fail ("PATH!s are immutable (convert to GROUP! or BLOCK! to mutate)");

    REBINT n;

    if (IS_INTEGER(picker) or IS_DECIMAL(picker)) { // #2312
        n = Int32(picker);
        if (n == 0)
            return nullptr; // Rebol2/Red convention: 0 is not a pick
        n = n - 1;
    }
    else
        fail (rebUnrelativize(picker));

    if (n < 0 or n >= cast(REBINT, VAL_SEQUENCE_LEN(pvs->out)))
        return nullptr;

    REBSPC *specifier = VAL_SEQUENCE_SPECIFIER(pvs->out);
    const RELVAL *at = VAL_SEQUENCE_AT(FRM_SPARE(pvs), pvs->out, n);

    return Derelativize(pvs->out, at, specifier);
}


//
//  MF_Sequence: C
//
void MF_Sequence(REB_MOLD *mo, REBCEL(const*) v, bool form)
{
    UNUSED(form);

    enum Reb_Kind kind = CELL_TYPE(v);  // Note: CELL_KIND() might be WORD!
    char interstitial = ANY_TUPLE_KIND(kind) ? '.' : '/';

    if (kind == REB_GET_PATH or kind == REB_GET_TUPLE)
        Append_Codepoint(mo->series, ':');
    else if (kind == REB_SYM_PATH or kind == REB_SYM_TUPLE)
        Append_Codepoint(mo->series, '@');

    bool first = true;

    DECLARE_LOCAL (temp);
    REBLEN len = VAL_SEQUENCE_LEN(v);
    REBLEN i;
    for (i = 0; i < len; ++i) {
        const RELVAL *element = VAL_SEQUENCE_AT(temp, v, i);
        enum Reb_Kind element_kind = VAL_TYPE(element);

        if (first)
            first = false;  // don't print `.` or `/` before first element
        else
            Append_Codepoint(mo->series, interstitial);

        if (element_kind != REB_BLANK) {  // no blank molding; implicit
            Mold_Value(mo, element);

            // Note: Ignore VALUE_FLAG_NEWLINE_BEFORE here for ANY-PATH,
            // but any embedded BLOCK! or GROUP! which do have newlines in
            // them can make newlines, e.g.:
            //
            //     a/[
            //        b c d
            //     ]/e
        }

    }

    if (kind == REB_SET_PATH or kind == REB_SET_TUPLE)
        Append_Codepoint(mo->series, ':');
}
