//
//  File: %l-types.c
//  Summary: "special lexical type converters"
//  Section: lexical
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
#include "sys-dec-to-char.h"
#include <errno.h>


//
// The scanning code in R3-Alpha used nullptr to return failure during the scan
// of a value, possibly leaving the value itself in an incomplete or invalid
// state.  Rather than write stray incomplete values into these spots, Ren-C
// puts "unreadable blank"
//

#define return_NULL \
    do { Erase_Cell(out); return nullptr; } while (1)


//
//  MAKE_Fail: C
//
Bounce MAKE_Fail(Value* out, enum Reb_Kind kind, const Value* arg)
{
    UNUSED(out);
    UNUSED(kind);
    UNUSED(arg);

    fail ("Datatype does not have a MAKE handler registered");
}


//
//  MAKE_Unhooked: C
//
// MAKE STRUCT! is part of the FFI extension, but since user defined types
// aren't ready yet as a general concept, this hook is overwritten in the
// dispatch table when the extension loads.
//
Bounce MAKE_Unhooked(Value* out, enum Reb_Kind kind, const Value* arg)
{
    UNUSED(out);
    UNUSED(arg);

    const Value* type = Datatype_From_Kind(kind);
    UNUSED(type); // !!! put in error message?

    fail ("Datatype is provided by an extension that's not currently loaded");
}


//
//  make: native [
//
//  {Constructs or allocates the specified datatype.}
//
//      return: [any-value!]
//          {Constructed value, or NULL if BLANK! input}
//      type [<maybe> datatype! event! any-context!]
//          {The datatype -or- an examplar value of the type to construct}
//      def [<maybe> any-element!]
//          {Definition or size of the new value (binding may be modified)}
//  ]
//
DECLARE_NATIVE(MAKE)
//
// 1. !!! The bootstrap executable was created in the midst of some strange
//    ideas about MAKE and CONSTRUCT.  MAKE was not allowed to take an
//    instance as the "spec", and CONSTRUCT was the weird arity-2 function
//    that could do that.  This had to be unwound, and it's not methodized
//    in a clear way...just hacked back to support the instances.
{
    INCLUDE_PARAMS_OF_MAKE;

    Value* type = ARG(TYPE);
    Value* arg = ARG(DEF);

    if (Is_Event(type)) {  // an event instance, not EVENT! datatype
        if (not Is_Block(arg))
            fail (Error_Bad_Make(TYPE_EVENT, arg));

        Copy_Cell(OUT, type); // !!! very "shallow" clone of the event
        Set_Event_Vars(
            OUT,
            Cell_List_At(arg),
            VAL_SPECIFIER(arg)
        );
        return OUT;
    }

    if (Any_Context(type))  // object instance, not a datatype
        return MAKE_With_Parent(OUT, Type_Of(type), arg, type);

    enum Reb_Kind kind;
    if (Is_Datatype(type))
        kind = CELL_DATATYPE_TYPE(type);
    else
        kind = Type_Of(type);

#if RUNTIME_CHECKS
    if (Is_Event(type)) {
        //
        // !!! It seems that EVENTs had some kind of inheritance mechanism, by
        // which you would write:
        //
        //     event1: make event! [...]
        //     event2: make event1 [...]
        //
        // The new plan is that MAKE operates on a definition spec, and that
        // this type slot is always a value or exemplar.  So if the feature
        // is needed, it should be something like:
        //
        //     event1: make event! [...]
        //     event2: make event! [event1 ...]
        //
        // Or perhaps not use make at all, but some other operation.
        //
        assert(false);
    }
#endif

    MAKE_HOOK hook = Make_Hooks[kind];
    if (hook == nullptr)
        fail (Error_Bad_Make(kind, arg));

    Bounce bounce = hook(OUT, kind, arg);  // might throw, fail...
    if (bounce == BOUNCE_THROWN)
        return bounce;
    if (bounce == nullptr or Type_Of(bounce) != kind)
        fail ("MAKE dispatcher did not return correct type");
    return bounce;  // may be OUT or an API handle
}


//
//  TO_Fail: C
//
Bounce TO_Fail(Value* out, enum Reb_Kind kind, const Value* arg)
{
    UNUSED(out);
    UNUSED(kind);
    UNUSED(arg);

    fail ("Cannot convert to datatype");
}


//
//  TO_Unhooked: C
//
Bounce TO_Unhooked(Value* out, enum Reb_Kind kind, const Value* arg)
{
    UNUSED(out);
    UNUSED(arg);

    const Value* type = Datatype_From_Kind(kind);
    UNUSED(type); // !!! put in error message?

    fail ("Datatype does not have extension with a TO handler registered");
}


//
//  to: native [
//
//  {Converts to a specified datatype, copying any underying data}
//
//      return: "VALUE converted to TYPE, null if type or value are blank"
//          [any-value!]
//      type [<maybe> datatype!]
//      value [<maybe> any-element!]
//  ]
//
DECLARE_NATIVE(TO)
{
    INCLUDE_PARAMS_OF_TO;

    Value* v = ARG(VALUE);
    enum Reb_Kind new_kind = CELL_DATATYPE_TYPE(ARG(TYPE));

    TO_HOOK hook = To_Hooks[new_kind];
    if (not hook)
        fail (Error_Invalid(v));

    Bounce bounce = hook(OUT, new_kind, v);  // may fail();
    if (bounce == BOUNCE_THROWN) {
        assert(!"Illegal throw in TO conversion handler");
        fail (Error_No_Catch_For_Throw(OUT));
    }
    if (bounce == nullptr or Type_Of(bounce) != new_kind) {
        assert(!"TO conversion did not return intended type");
        fail (Error_Invalid_Type(Type_Of(bounce)));
    }
    return bounce; // must be either OUT or an API handle
}


//
//  REBTYPE: C
//
// There's no actual "Unhooked" data type, it is used as a placeholder for
// if a datatype (such as STRUCT!) is going to have its behavior loaded by
// an extension.
//
REBTYPE(Unhooked)
{
    UNUSED(level_);
    UNUSED(verb);

    fail ("Datatype does not have its REBTYPE() handler loaded by extension");
}


// !!! Some reflectors are more general and apply to all types (e.g. TYPE)
// while others only apply to some types (e.g. LENGTH or HEAD only to series,
// or perhaps things like PORT! that wish to act like a series).  This
// suggests a need for a kind of hierarchy of handling.
//
// The series common code is in Series_Common_Action_Maybe_Unhandled(), but
// that is only called from series.  Handle a few extra cases here.
//
Bounce Reflect_Core(Level* level_)
{
    INCLUDE_PARAMS_OF_REFLECT;

    enum Reb_Kind kind = Type_Of(ARG(VALUE));

    Option(SymId) id = Cell_Word_Id(ARG(PROPERTY));

    if (not id) {
        //
        // If a word wasn't in %words.r, it has no integer SYM.  There is
        // no way for a built-in reflector to handle it...since they just
        // operate on SYMs in a switch().  Longer term, a more extensible
        // idea will be necessary.
        //
        fail (Error_Cannot_Reflect(kind, ARG(PROPERTY)));
    }

    switch (id) {
    case SYM_TYPE:
        if (kind == TYPE_NULLED)
            return nullptr; // `() = type of ()`, `null = type of ()`

        return Init_Datatype(OUT, kind);;

    default:
        // !!! Are there any other universal reflectors?
        break;
    }

    // !!! The reflector for TYPE is universal and so it is allowed on nulls,
    // but in general actions should not allow null first arguments...there's
    // no entry in the dispatcher table for them.
    //
    if (kind == TYPE_NULLED)
        fail ("NULL isn't valid for REFLECT, except for TYPE OF ()");

    GENERIC_HOOK hook = Generic_Hooks[kind];
    DECLARE_VALUE (verb);
    Init_Word(verb, Canon(SYM_REFLECT));
    return hook(level_, verb);
}


//
//  reflect: native [
//
//  {Returns specific details about a datatype.}
//
//      return: [any-value!]
//      value "Accepts NULL so REFLECT () 'TYPE can be returned as NULL"
//          [any-value!]
//      property [word!]
//          "Such as: type, length, spec, body, words, values, title"
//  ]
//
DECLARE_NATIVE(REFLECT)
//
// Although REFLECT goes through dispatch to the REBTYPE(), it was needing
// a null check in Type_Action_Dispatcher--which no other type needs.  So
// it is its own native.  Consider giving it its own dispatcher as well, as
// the question of exactly what a "REFLECT" or "OF" actually *is*.
{
    return Reflect_Core(level_);
}


//
//  of: infix native [
//
//  {Infix form of REFLECT which quotes its left (X OF Y => REFLECT Y 'X)}
//
//      return: [any-value!]
//      'property [word!]
//      value "Accepts NULL so TYPE OF () can be returned as NULL"
//          [any-value!]
//  ]
//
DECLARE_NATIVE(OF)
//
// Common enough to be worth it to do some kind of optimization so it's not
// much slower than a REFLECT; e.g. you don't want it building a separate
// frame to make the REFLECT call in just because of the parameter reorder.
{
    INCLUDE_PARAMS_OF_OF;

    // !!! Ugly hack to make OF frame-compatible with REFLECT.  If there was
    // a separate dispatcher for REFLECT it could be called with proper
    // parameterization, but as things are it expects the arguments to
    // fit the type action dispatcher rule... dispatch item in first arg,
    // property in the second.
    //
    DECLARE_VALUE (temp);
    Copy_Cell(temp, ARG(PROPERTY));
    Copy_Cell(ARG(PROPERTY), ARG(VALUE));
    Copy_Cell(ARG(VALUE), temp);

    return Reflect_Core(level_);
}


//
//  Scan_Hex: C
//
// Scans hex while it is valid and does not exceed the maxlen.
// If the hex string is longer than maxlen - it's an error.
// If a bad char is found less than the minlen - it's an error.
// String must not include # - ~ or other invalid chars.
// If minlen is zero, and no string, that's a valid zero value.
//
// Note, this function relies on LEX_WORD lex values having a LEX_VALUE
// field of zero, except for hex values.
//
const Byte *Scan_Hex(
    Value* out,
    const Byte *cp,
    REBLEN minlen,
    REBLEN maxlen
) {
    assert(Is_Cell_Erased(out));

    if (maxlen > MAX_HEX_LEN)
        return_NULL;

    REBI64 i = 0;
    REBLEN cnt = 0;
    Byte lex;
    while ((lex = g_lex_map[*cp]) > LEX_WORD) {
        Byte v;
        if (++cnt > maxlen)
            return_NULL;
        v = cast(Byte, lex & LEX_VALUE); // char num encoded into lex
        if (!v && lex < LEX_NUMBER)
            return_NULL;  // invalid char (word but no val)
        i = (i << 4) + v;
        cp++;
    }

    if (cnt < minlen)
        return_NULL;

    Init_Integer(out, i);
    return cp;
}


//
//  Scan_Hex2: C
//
// Decode a %xx hex encoded byte into a char.
//
// The % should already be removed before calling this.
//
// We don't allow a %00 in files, urls, email, etc... so
// a return of 0 is used to indicate an error.
//
bool Scan_Hex2(Ucs2Unit* out, const void *p, bool unicode)
{
    Ucs2Unit c1;
    Ucs2Unit c2;
    if (unicode) {
        const Ucs2Unit* up = cast(const Ucs2Unit*, p);
        c1 = up[0];
        c2 = up[1];
    }
    else {
        const Byte *bp = cast(const Byte*, p);
        c1 = bp[0];
        c2 = bp[1];
    }

    Byte lex1 = g_lex_map[c1];
    Byte d1 = lex1 & LEX_VALUE;
    if (lex1 < LEX_WORD || (d1 == 0 && lex1 < LEX_NUMBER))
        return false;

    Byte lex2 = g_lex_map[c2];
    Byte d2 = lex2 & LEX_VALUE;
    if (lex2 < LEX_WORD || (d2 == 0 && lex2 < LEX_NUMBER))
        return false;

    *out = cast(Ucs2Unit, (d1 << 4) + d2);

    return true;
}


//
//  Scan_Dec_Buf: C
//
// Validate a decimal number. Return on first invalid char (or end).
// Returns nullptr if not valid.
//
// Scan is valid for 1 1.2 1,2 1'234.5 1x 1.2x 1% 1.2% etc.
//
// !!! Is this redundant with Scan_Decimal?  Appears to be similar code.
//
const Byte *Scan_Dec_Buf(
    Byte *out, // may live in data stack (do not call DS_PUSH, GC, eval)
    bool *found_point,  // found a comma or a dot
    const Byte *cp,
    REBLEN len // max size of buffer
) {
    assert(len >= MAX_NUM_LEN);
    *found_point = false;

    Byte *bp = out;
    Byte *be = bp + len - 1;

    if (*cp == '+' || *cp == '-')
        *bp++ = *cp++;

    bool digit_present = false;
    while (Is_Lex_Number(*cp) || *cp == '\'') {
        if (*cp != '\'') {
            *bp++ = *cp++;
            if (bp >= be)
                return nullptr;
            digit_present = true;
        }
        else
            ++cp;
    }

    if (*cp == ',' || *cp == '.') {
        *found_point = true;
        cp++;
    }

    *bp++ = '.';
    if (bp >= be)
        return nullptr;

    while (Is_Lex_Number(*cp) || *cp == '\'') {
        if (*cp != '\'') {
            *bp++ = *cp++;
            if (bp >= be)
                return nullptr;
            digit_present = true;
        }
        else
            ++cp;
    }

    if (not digit_present)
        return nullptr;

    if (*cp == 'E' || *cp == 'e') {
        *bp++ = *cp++;
        if (bp >= be)
            return nullptr;

        digit_present = false;

        if (*cp == '-' || *cp == '+') {
            *bp++ = *cp++;
            if (bp >= be)
                return nullptr;
        }

        while (Is_Lex_Number(*cp)) {
            *bp++ = *cp++;
            if (bp >= be)
                return nullptr;
            digit_present = true;
        }

        if (not digit_present)
            return nullptr;
    }

    *bp = '\0';
    return cp;
}


//
//  Scan_Decimal: C
//
// Scan and convert a decimal value.  Return zero if error.
//
const Byte *Scan_Decimal(
    Value* out, // may live in data stack (do not call DS_PUSH, GC, eval)
    const Byte *cp,
    REBLEN len,
    bool dec_only
) {
    assert(Is_Cell_Erased(out));

    Byte buf[MAX_NUM_LEN + 4];
    Byte *ep = buf;
    if (len > MAX_NUM_LEN)
        return_NULL;

    const Byte *bp = cp;

    if (*cp == '+' || *cp == '-')
        *ep++ = *cp++;

    bool digit_present = false;

    while (Is_Lex_Number(*cp) || *cp == '\'') {
        if (*cp != '\'') {
            *ep++ = *cp++;
            digit_present = true;
        }
        else
            ++cp;
    }

    if (*cp == ',' || *cp == '.')
        ++cp;

    *ep++ = '.';

    while (Is_Lex_Number(*cp) || *cp == '\'') {
        if (*cp != '\'') {
            *ep++ = *cp++;
            digit_present = true;
        }
        else
            ++cp;
    }

    if (not digit_present)
        return_NULL;

    if (*cp == 'E' || *cp == 'e') {
        *ep++ = *cp++;
        digit_present = false;

        if (*cp == '-' || *cp == '+')
            *ep++ = *cp++;

        while (Is_Lex_Number(*cp)) {
            *ep++ = *cp++;
            digit_present = true;
        }

        if (not digit_present)
            return_NULL;
    }

    if (*cp == '%') {
        if (dec_only)
            return_NULL;

        ++cp; // ignore it
    }

    *ep = '\0';

    if (cast(REBLEN, cp - bp) != len)
        return_NULL;

    RESET_CELL(out, TYPE_DECIMAL);

    char *se;
    VAL_DECIMAL(out) = strtod(s_cast(buf), &se);

    // !!! TBD: need check for NaN, and INF

    if (fabs(VAL_DECIMAL(out)) == HUGE_VAL)
        fail (Error_Overflow_Raw());

    return cp;
}


//
//  Scan_Integer: C
//
// Scan and convert an integer value.  Return zero if error.
// Allow preceding + - and any combination of ' marks.
//
const Byte *Scan_Integer(
    Value* out, // may live in data stack (do not call DS_PUSH, GC, eval)
    const Byte *cp,
    REBLEN len
) {
    assert(Is_Cell_Erased(out));

    // Super-fast conversion of zero and one (most common cases):
    if (len == 1) {
        if (*cp == '0') {
            Init_Integer(out, 0);
            return cp + 1;
        }
        if (*cp == '1') {
            Init_Integer(out, 1);
            return cp + 1;
         }
    }

    Byte buf[MAX_NUM_LEN + 4];
    if (len > MAX_NUM_LEN)
        return_NULL; // prevent buffer overflow

    Byte *bp = buf;

    bool neg = false;

    REBINT num = cast(REBINT, len);

    // Strip leading signs:
    if (*cp == '-') {
        *bp++ = *cp++;
        --num;
        neg = true;
    }
    else if (*cp == '+') {
        ++cp;
        --num;
    }

    // Remove leading zeros:
    for (; num > 0; num--) {
        if (*cp == '0' || *cp == '\'')
            ++cp;
        else
            break;
    }

    if (num == 0) { // all zeros or '
        // return early to avoid platform dependant error handling in CHR_TO_INT
        Init_Integer(out, 0);
        return cp;
    }

    // Copy all digits, except ' :
    for (; num > 0; num--) {
        if (*cp >= '0' && *cp <= '9')
            *bp++ = *cp++;
        else if (*cp == '\'')
            ++cp;
        else
            return_NULL;
    }
    *bp = '\0';

    // Too many digits?
    len = bp - &buf[0];
    if (neg)
        --len;
    if (len > 19) {
        // !!! magic number :-( How does it relate to MAX_INT_LEN (also magic)
        return_NULL;
    }

    // Convert, check, and return:
    errno = 0;

    RESET_CELL(out, TYPE_INTEGER);

    VAL_INT64(out) = CHR_TO_INT(buf);
    if (errno != 0)
        return_NULL; // overflow

    if ((VAL_INT64(out) > 0 && neg) || (VAL_INT64(out) < 0 && !neg))
        return_NULL;

    return cp;
}


//
//  Scan_Date: C
//
// Scan and convert a date. Also can include a time and zone.
//
const Byte *Scan_Date(
    Value* out, // may live in data stack (do not call DS_PUSH, GC, eval)
    const Byte *cp,
    REBLEN len
) {
    assert(Is_Cell_Erased(out));

    const Byte *end = cp + len;

    // Skip spaces:
    for (; *cp == ' ' && cp != end; cp++);

    // Skip day name, comma, and spaces:
    const Byte *ep;
    for (ep = cp; *ep != ',' && ep != end; ep++);
    if (ep != end) {
        cp = ep + 1;
        while (*cp == ' ' && cp != end) cp++;
    }
    if (cp == end)
        return_NULL;

    REBINT num;

    // Day or 4-digit year:
    ep = Grab_Int(cp, &num);
    if (num < 0)
        return_NULL;

    REBINT day;
    REBINT month;
    REBINT year;
    REBINT tz;

    REBLEN size = cast(REBLEN, ep - cp);
    if (size >= 4) {
        // year is set in this branch (we know because day is 0)
        // Ex: 2009/04/20/19:00:00+0:00
        year = num;
        day = 0;
    }
    else if (size) {
        // year is not set in this branch (we know because day ISN'T 0)
        // Ex: 12-Dec-2012
        day = num;
        if (day == 0)
            return_NULL;

        // !!! Clang static analyzer doesn't know from test of `day` below
        // how it connects with year being set or not.  Suppress warning.
        year = INT32_MIN; // !!! Garbage, should not be read.
    }
    else
        return_NULL;

    cp = ep;

    // Determine field separator:
    if (*cp != '/' && *cp != '-' && *cp != '.' && *cp != ' ')
        return_NULL;

    Byte sep = *cp++;

    // Month as number or name:
    ep = Grab_Int(cp, &num);
    if (num < 0)
        return_NULL;

    size = cast(REBLEN, ep - cp);

    if (size > 0)
        month = num; // got a number
    else { // must be a word
        for (ep = cp; Is_Lex_Word(*ep); ep++)
            NOOP; // scan word

        size = cast(REBLEN, ep - cp);
        if (size < 3)
            return_NULL;

        for (num = 0; num < 12; num++) {
            if (!Compare_Bytes(cb_cast(Month_Names[num]), cp, size, true))
                break;
        }
        month = num + 1;
    }

    if (month < 1 || month > 12)
        return_NULL;

    cp = ep;
    if (*cp++ != sep)
        return_NULL;

    // Year or day (if year was first):
    ep = Grab_Int(cp, &num);
    if (*cp == '-' || num < 0)
        return_NULL;

    size = cast(REBLEN, ep - cp);
    if (size == 0)
        return_NULL;

    if (day == 0) {
        // year already set, but day hasn't been
        day = num;
    }
    else {
        // day has been set, but year hasn't been.
        if (size >= 3)
            year = num;
        else {
            // !!! Originally this allowed shorthands, so that 96 = 1996, etc.
            //
            //     if (num >= 70)
            //         year = 1900 + num;
            //     else
            //         year = 2000 + num;
            //
            // It was trickier than that, because it actually used the current
            // year (from the clock) to guess what the short year meant.  That
            // made it so the scanner would scan the same source code
            // differently based on the clock, which is bad.  By allowing
            // short dates to be turned into their short year equivalents, the
            // user code can parse such dates and fix them up after the fact
            // according to their requirements, `if date/year < 100 [...]`
            //
            year = num;
        }
    }

    if (year > MAX_YEAR || day < 1 || day > Month_Max_Days[month-1])
        return_NULL;

    // Check February for leap year or century:
    if (month == 2 && day == 29) {
        if (
            ((year % 4) != 0) ||        // not leap year
            ((year % 100) == 0 &&       // century?
            (year % 400) != 0)
        ){
            return_NULL; // not leap century
        }
    }

    cp = ep;

    if (cp >= end) {
        RESET_CELL(out, TYPE_DATE);
        goto end_date; // needs header set
    }

    if (*cp == '/' || *cp == ' ') {
        sep = *cp++;

        if (cp >= end) {
            RESET_CELL(out, TYPE_DATE);
            goto end_date; // needs header set
        }

        cp = Scan_Time(out, cp, 0);
        if (
            cp == nullptr
            or not Is_Time(out)
            or VAL_NANO(out) < 0
            or VAL_NANO(out) >= SECS_TO_NANO(24 * 60 * 60)
        ){
            return_NULL;
        }

        Reset_Cell_Header(out, TYPE_DATE, CELL_FLAG_DATE_HAS_TIME);
    }
    else
        RESET_CELL(out, TYPE_DATE); // no CELL_FLAG_DATE_HAS_TIME

    // past this point, header is set, so `goto end_date` is legal.

    if (*cp == sep)
        ++cp;

    // Time zone can be 12:30 or 1230 (optional hour indicator)
    if (*cp == '-' || *cp == '+') {
        if (cp >= end)
            goto end_date;

        ep = Grab_Int(cp + 1, &num);
        if (ep - cp == 0)
            return_NULL;

        if (*ep != ':') {
            if (num < -1500 || num > 1500)
                return_NULL;

            int h = (num / 100);
            int m = (num - (h * 100));

            tz = (h * 60 + m) / ZONE_MINS;
        }
        else {
            if (num < -15 || num > 15)
                return_NULL;

            tz = num * (60 / ZONE_MINS);

            if (*ep == ':') {
                ep = Grab_Int(ep + 1, &num);
                if (num % ZONE_MINS != 0)
                    return_NULL;

                tz += num / ZONE_MINS;
            }
        }

        if (ep != end)
            return_NULL;

        if (*cp == '-')
            tz = -tz;

        cp = ep;

        Set_Cell_Flag(out, DATE_HAS_ZONE);
        INIT_VAL_ZONE(out, tz);
    }

end_date:
    assert(Is_Date(out)); // don't reset header here; overwrites flags
    VAL_YEAR(out)  = year;
    VAL_MONTH(out) = month;
    VAL_DAY(out) = day;

    // if VAL_NANO() was set, then CELL_FLAG_DATE_HAS_TIME should be true
    // if VAL_ZONE() was set, then CELL_FLAG_DATE_HAS_ZONE should be true

    // This step used to be skipped if tz was 0, but now that is a
    // state distinguished from "not having a time zone"
    //
    Adjust_Date_Zone(out, true);

    return cp;
}


//
//  Scan_File_Or_Money: C
//
// Scan and convert a file name or MONEY!
//
const Byte *Scan_File_Or_Money(
    Value* out, // may live in data stack (do not call DS_PUSH, GC, eval)
    const Byte *bp,
    REBLEN len
) {
    assert(Is_Cell_Erased(out));
    assert(*bp == '%' or *bp == '$');

    const Byte* cp = bp + 1;
    --len;

    Ucs2Unit term = 0;
    const Byte *invalid;
    if (*cp == '"') {
        cp++;
        len--;
        term = '"';
        invalid = cb_cast(":;\"");
    }
    else {
        term = 0;
        invalid = cb_cast(":;()[]\"");
    }

    DECLARE_MOLDER (mo);

    cp = Scan_Item_Push_Mold(mo, cp, cp + len, term, invalid);
    if (cp == nullptr) {
        Drop_Mold(mo);
        return_NULL;
    }

    Init_Any_Series(
        out,
        *bp == '$' ? TYPE_MONEY : TYPE_FILE,
        Pop_Molded_String(mo)
    );
    return cp;
}


//
//  Scan_Email: C
//
// Scan and convert email.
//
const Byte *Scan_Email(
    Value* out, // may live in data stack (do not call DS_PUSH, GC, eval)
    const Byte *cp,
    REBLEN len
) {
    assert(Is_Cell_Erased(out));

    String* s = Make_String(len);
    Ucs2(*) up = String_Head(s);

    REBLEN num_chars = 0;

    bool found_at = false;
    for (; len > 0; len--) {
        if (*cp == '@') {
            if (found_at)
                return_NULL;
            found_at = true;
        }

        if (*cp == '%') {
            const bool unicode = false;
            Ucs2Unit ch;
            if (len <= 2 || !Scan_Hex2(&ch, cp + 1, unicode))
                return_NULL;

            up = Write_Codepoint(up, ch);
            ++num_chars;

            cp += 3;
            len -= 2;
        }
        else {
            up = Write_Codepoint(up, *cp++);
            ++num_chars;
        }
    }

    if (not found_at)
        return_NULL;

    Term_String_Len(s, num_chars);

    Init_Email(out, s);
    return cp;
}


//
//  Scan_URL: C
//
// While Rebol2, R3-Alpha, and Red attempted to apply some amount of decoding
// (e.g. how %20 is "space" in http:// URL!s), Ren-C leaves URLs "as-is".
// This means a URL may be copied from a web browser bar and pasted back.
// It also means that the URL may be used with custom schemes (odbc://...)
// that have different ideas of the meaning of characters like `%`.
//
// !!! The current concept is that URL!s typically represent the *decoded*
// forms, and thus express unicode codepoints normally...preserving either of:
//
//     https://duckduckgo.com/?q=hergé+&+tintin
//     https://duckduckgo.com/?q=hergé+%26+tintin
//
// Then, the encoded forms with UTF-8 bytes expressed in %XX form would be
// converted as STRING!, where their datatype suggests the encodedness:
//
//     {https://duckduckgo.com/?q=herg%C3%A9+%26+tintin}
//
// (This is similar to how local FILE!s, where e.g. slashes become backslash
// on Windows, are expressed as STRING!.)
//
const Byte *Scan_URL(
    Value* out, // may live in data stack (do not call DS_PUSH, GC, eval)
    const Byte *cp,
    REBLEN len
){
    return Scan_Any(out, cp, len, TYPE_URL);
}


//
//  Scan_Pair: C
//
// Scan and convert a pair
//
const Byte *Scan_Pair(
    Value* out, // may live in data stack (do not call DS_PUSH, GC, eval)
    const Byte *cp,
    REBLEN len
) {
    assert(Is_Cell_Erased(out));

    Byte buf[MAX_NUM_LEN + 4];

    bool found_x_point;
    const Byte *ep = Scan_Dec_Buf(&buf[0], &found_x_point, cp, MAX_NUM_LEN);
    if (ep == nullptr)
        return_NULL;
    if (*ep != 'x' && *ep != 'X')
        return_NULL;

    RESET_CELL(out, TYPE_PAIR);
    out->payload.pair = Alloc_Pairing();
    RESET_CELL(out->payload.pair, TYPE_DECIMAL);
    RESET_CELL(PAIRING_KEY(out->payload.pair), TYPE_DECIMAL);

    if (found_x_point)
        Init_Decimal(VAL_PAIR_FIRST(out), atof(cast(char*, &buf[0])));
    else
        Init_Integer(VAL_PAIR_FIRST(out), atoi(cast(char*, &buf[0])));
    ep++;

    bool found_y_point;
    const Byte *xp = Scan_Dec_Buf(&buf[0], &found_y_point, ep, MAX_NUM_LEN);
    if (!xp) {
        Free_Pairing(out->payload.pair);
        return_NULL;
    }

    if (found_y_point)
        Init_Decimal(VAL_PAIR_SECOND(out), atof(cast(char*, &buf[0])));
    else
        Init_Integer(VAL_PAIR_SECOND(out), atoi(cast(char*, &buf[0])));

    if (len > cast(REBLEN, xp - cp)) {
        Free_Pairing(out->payload.pair);
        return_NULL;
    }

    Manage_Pairing(out->payload.pair);
    return xp;
}


//
//  Scan_Tuple: C
//
// Scan and convert a tuple.
//
const Byte *Scan_Tuple(
    Value* out, // may live in data stack (do not call DS_PUSH, GC, eval)
    const Byte *cp,
    REBLEN len
) {
    assert(Is_Cell_Erased(out));

    if (len == 0)
        return_NULL;

    const Byte *ep;
    REBLEN size = 1;
    REBINT n;
    for (n = cast(REBINT, len), ep = cp; n > 0; n--, ep++) { // count '.'
        if (*ep == '.')
            ++size;
    }

    if (size > MAX_TUPLE)
        return_NULL;

    if (size < 3)
        size = 3;

    RESET_CELL(out, TYPE_TUPLE);
    VAL_TUPLE_LEN(out) = cast(Byte, size);

    Byte *tp = VAL_TUPLE(out);
    memset(tp, 0, sizeof(REBTUP) - 2);

    for (ep = cp; len > cast(REBLEN, ep - cp); ++ep) {
        ep = Grab_Int(ep, &n);
        if (n < 0 || n > 255)
            return_NULL;

        *tp++ = cast(Byte, n);
        if (*ep != '.')
            break;
    }

    if (len > cast(REBLEN, ep - cp))
        return_NULL;

    return ep;
}


//
//  Scan_Binary: C
//
// Scan and convert binary strings.
//
const Byte *Scan_Binary(
    Value* out, // may live in data stack (do not call DS_PUSH, GC, eval)
    const Byte *cp,
    REBLEN len
) {
    assert(Is_Cell_Erased(out));

    REBINT base = 16;

    if (*cp != '#') {
        const Byte *ep = Grab_Int(cp, &base);
        if (cp == ep || *ep != '#')
            return_NULL;
        len -= cast(REBLEN, ep - cp);
        cp = ep;
    }

    cp++;  // skip #
    if (*cp++ != '{')
        return_NULL;

    len -= 2;

    cp = Decode_Binary(out, cp, len, base, '}');
    if (cp == nullptr)
        return_NULL;

    cp = Skip_To_Byte(cp, cp + len, '}');
    if (cp == nullptr)
        return_NULL; // series will be gc'd

    return cp + 1; // include the "}" in the scan total
}


//
//  Scan_Any: C
//
// Scan any string that does not require special decoding.
//
const Byte *Scan_Any(
    Value* out, // may live in data stack (do not call DS_PUSH, GC, eval)
    const Byte *cp,
    REBLEN num_bytes,
    enum Reb_Kind type
) {
    assert(Is_Cell_Erased(out));

    // The range for a curly braced string may span multiple lines, and some
    // files may have CR and LF in the data:
    //
    //     {line one ;-- imagine this is CR LF...not just LF
    //     line two}
    //
    // Despite the presence of the CR in the source file, the scanned literal
    // should only support LF (if it supports files with it at all)
    //
    // http://blog.hostilefork.com/death-to-carriage-return/
    //
    bool crlf_to_lf = true;

    Flex* s = Append_UTF8_May_Fail(nullptr, cs_cast(cp), num_bytes, crlf_to_lf);
    Init_Any_Series(out, type, s);

    return cp + num_bytes;
}


//
//  scan-net-header: native [
//      {Scan an Internet-style header (HTTP, SMTP).}
//
//      header [binary!]
//          {Fields with duplicate words will be merged into a block.}
//  ]
//
DECLARE_NATIVE(SCAN_NET_HEADER)
//
// !!! This routine used to be a feature of CONSTRUCT in R3-Alpha, and was
// used by %prot-http.r.  The idea was that instead of providing a parent
// object, a STRING! or BINARY! could be provided which would be turned
// into a block by this routine.
//
// It doesn't make much sense to have this coded in C rather than using PARSE
// It's only being converted into a native to avoid introducing bugs by
// rewriting it as Rebol in the middle of other changes.
{
    INCLUDE_PARAMS_OF_SCAN_NET_HEADER;

    Array* result = Make_Array(10); // Just a guess at size (use STD_BUF?)

    Value* header = ARG(HEADER);
    REBLEN index = VAL_INDEX(header);
    Binary* utf8 = Cell_Binary(header);

    Byte *cp = Binary_Head(utf8) + index;

    while (Is_Lex_Whitespace(*cp)) cp++; // skip white space

    Byte *start;
    REBINT len;

    while (true) {
        // Scan valid word:
        if (Is_Lex_Word(*cp)) {
            start = cp;
            while (
                Is_Lex_Word_Or_Number(*cp)
                || *cp == '.'
                || *cp == '-'
                || *cp == '_'
            ) {
                cp++;
            }
        }
        else break;

        if (*cp != ':')
            break;

        Value* val = nullptr; // rigorous checks worry it could be uninitialized

        Symbol* name = Intern_UTF8_Managed(start, cp - start);
        Cell* item;

        cp++;
        // Search if word already present:
        for (item = Array_Head(result); NOT_END(item); item += 2) {
            assert(Is_Text(item + 1) || Is_Block(item + 1));
            if (Are_Synonyms(Cell_Word_Symbol(item), name)) {
                // Does it already use a block?
                if (Is_Block(item + 1)) {
                    // Block of values already exists:
                    val = Init_Unreadable(
                        Alloc_Tail_Array(Cell_Array(item + 1))
                    );
                }
                else {
                    // Create new block for values:
                    Array* a = Make_Array(2);
                    Derelativize(
                        Alloc_Tail_Array(a),
                        item + 1, // prior value
                        SPECIFIED // no relative values added
                    );
                    val = Init_Unreadable(Alloc_Tail_Array(a));
                    Init_Block(item + 1, a);
                }
                break;
            }
        }

        if (IS_END(item)) { // didn't break, add space for new word/value
            Init_Set_Word(Alloc_Tail_Array(result), name);
            val = Init_Unreadable(Alloc_Tail_Array(result));
        }

        while (Is_Lex_Space(*cp)) cp++;
        start = cp;
        len = 0;
        while (!ANY_CR_LF_END(*cp)) {
            len++;
            cp++;
        }
        // Is it continued on next line?
        while (*cp) {
            if (*cp == CR)
                ++cp;
            if (*cp == LF)
                ++cp;
            if (not Is_Lex_Space(*cp))
                break;
            while (Is_Lex_Space(*cp))
                ++cp;
            while (!ANY_CR_LF_END(*cp)) {
                ++len;
                ++cp;
            }
        }

        // Create string value (ignoring lines and indents):
        //
        // !!! This is written to deal with unicode lengths in terms of *size*
        // in bytes, not *length* in characters.  If it were to be done
        // correctly, it would need to use NEXT_CHR to count the characters
        // in the loop above.  Better to convert to usermode.

        String* string = Make_String(len);
        Ucs2(*) str = String_Head(string);
        cp = start;

        // "Code below *MUST* mirror that above:"

        while (!ANY_CR_LF_END(*cp))
            str = Write_Codepoint(str, *cp++);
        while (*cp != '\0') {
            if (*cp == CR)
                ++cp;
            if (*cp == LF)
                ++cp;
            if (not Is_Lex_Space(*cp))
                break;
            while (Is_Lex_Space(*cp))
                ++cp;
            while (!ANY_CR_LF_END(*cp))
                str = Write_Codepoint(str, *cp++);
        }
        Term_String_Len(string, len);
        Init_Text(val, string);
    }

    return Init_Block(OUT, result);
}
