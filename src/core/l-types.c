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
#include "sys-dec-to-char.h"
#include <errno.h>


//
//  DECLARE_GENERICS: C
//
// There's no actual "Unhooked" data type, it is used as a placeholder for
// if a datatype (such as STRUCT!) is going to have its behavior loaded by
// an extension.
//
DECLARE_GENERICS(Unhooked)
{
    UNUSED(verb);

    return RAISE(
        "Datatype does not have its DECLARE_GENERICS() handler loaded by extension"
    );
}


// !!! Some reflectors are more general and apply to all types (e.g. TYPE)
// while others only apply to some types (e.g. LENGTH or HEAD only to series,
// or perhaps things like PORT! that wish to act like a series).  This
// suggests a need for a kind of hierarchy of handling.
//
// 1. See remarks on Run_Generic_Dispatch() for why we don't allow things
//    like (3 = length of ''[a b c]).  An exception is made for action
//    antiforms, because they cannot be put in blocks, so their impact
//    is limited...and we want things like (label of append/) to be able
//    to work.  So only those are turned into the plain form here.
//
Bounce Reflect_Core(Level* level_)
{
    INCLUDE_PARAMS_OF_REFLECT;

    Value* v = ARG(value);

    Option(SymId) id = Cell_Word_Id(ARG(property));

    switch (id) {
      case SYM_HEART:
        if (Is_Nulled(v))
            return RAISE(Error_Type_Of_Null_Raw());  // caller can TRY if meant
        return Init_Builtin_Datatype(OUT, Cell_Heart(v));

      case SYM_TYPE:  // currently synonym for KIND, may change
        if (Is_Nulled(v))
            return RAISE(Error_Type_Of_Null_Raw());  // caller can TRY if meant
        return Init_Builtin_Datatype(OUT, VAL_TYPE(v));

      case SYM_QUOTES:
        return Init_Integer(OUT, Cell_Num_Quotes(v));

      case SYM_SIGIL: {
        if (Is_Antiform(v))
            return FAIL("Can't take SIGIL OF an antiform");

        Option(Sigil) sigil = Sigil_Of(cast(Element*, v));
        if (not sigil)
            return nullptr;
        return Init_Sigil(OUT, unwrap sigil); }

      default:
        // !!! Are there any other universal reflectors?
        break;
    }

    if (Is_Void(v))
        return nullptr;

    if (Is_Action(v))  // special exemption for action types [1]
        QUOTE_BYTE(v) = NOQUOTE_1;
    else if (QUOTE_BYTE(v) != NOQUOTE_1)
        fail ("REFLECT on quoted/quasi/anti only [HEART TYPE QUOTES SIGIL]");

    if (Any_Series_Kind(Cell_Heart(v))) {  // common for series
        switch (id) {
          case SYM_INDEX:
            return Init_Integer(OUT, VAL_INDEX_RAW(v) + 1);

          case SYM_LENGTH:
            return Init_Integer(OUT, Cell_Series_Len_At(v));

          case SYM_HEAD:
            Copy_Cell(OUT, v);
            VAL_INDEX_RAW(OUT) = 0;
            return Trust_Const(OUT);

          case SYM_TAIL:
            Copy_Cell(OUT, v);
            VAL_INDEX_RAW(OUT) = Cell_Series_Len_Head(v);
            return Trust_Const(OUT);

          case SYM_HEAD_Q:
            return Init_Logic(OUT, VAL_INDEX_RAW(v) == 0);

          case SYM_TAIL_Q:
            return Init_Logic(
                OUT,
                VAL_INDEX_RAW(v) == Cell_Series_Len_Head(v)
            );

          case SYM_PAST_Q:
            return Init_Logic(
                OUT,
                VAL_INDEX_RAW(v) > Cell_Series_Len_Head(v)
            );

          case SYM_FILE: {
            if (not Any_List(v))
                return nullptr;
            const Source* s = Cell_Array(v);
            if (Not_Source_Flag(s, HAS_FILE_LINE))
                return nullptr;
            Option(const String*) file = LINK(Filename, s);
            if (not file)
                return nullptr;
            return Init_File(OUT, unwrap file); }

          case SYM_LINE: {
            if (not Any_List(v))
                return nullptr;
            const Source* s = Cell_Array(v);
            if (Not_Source_Flag(s, HAS_FILE_LINE))
                return nullptr;
            return Init_Integer(OUT, s->misc.line); }

          default:
            break;
        }
    }

    return Run_Generic_Dispatch(cast(Element*, v), LEVEL, Canon(REFLECT));
}


//
//  /reflect: native:generic [
//
//  "Returns specific details about a value (used by OF, e.g. LENGTH OF)"
//
//      return: [any-value?]
//      value [any-value?]
//      property "Such as: type, length, spec, body, words, values, title"
//          [word!]
//  ]
//
DECLARE_NATIVE(reflect)
{
    return Reflect_Core(level_);
}


//
//  /of: infix native [
//
//  "Infix form of REFLECT which quotes its left (X OF Y => REFLECT Y 'X)"
//
//      return: [any-value?]
//      @(property) "Escapable slot for WORD!"
//          [word!]
//      value [any-value?]
//  ]
//
DECLARE_NATIVE(of)
//
// 1. Ugly hack to make OF frame-compatible with REFLECT.  If there was a
//    separate dispatcher for REFLECT it could be called with proper
//    parameterization, but as things are it expects the arguments to fit the
//    type action dispatcher rule... dispatch item in first arg, property in
//    the second.
//
// 2. OF is called often enough to be worth it to do some kind of optimization
//    so it's not much slower than a REFLECT; e.g. you don't want it building
//    a separate frame to make the REFLECT call in just because of the
//    parameter reorder.
{
    INCLUDE_PARAMS_OF_OF;

    Value* prop = ARG(property);
    assert(Is_Word(prop));

    Copy_Cell(SPARE, prop);
    Copy_Cell(ARG(property), ARG(value));  // frame compatibility [1]
    Copy_Cell(ARG(value), stable_SPARE);

    return Reflect_Core(level_);  // use same frame [2]
}


//
//  Try_Scan_Hex_Integer: C
//
// Scans hex while it is valid and does not exceed the maxlen.
// If the hex string is longer than maxlen - it's an error.
// If a bad char is found less than the minlen - it's an error.
// String must not include # - ~ or other invalid chars.
// If minlen is zero, and no string, that's a valid zero value.
//
Option(const Byte*) Try_Scan_Hex_Integer(
    Sink(Element) out,
    const Byte* cp,
    REBLEN minlen,
    REBLEN maxlen
){
    if (maxlen > MAX_HEX_LEN)
        return nullptr;

    REBI64 i = 0;
    REBLEN len = 0;
    Byte nibble;
    while (Try_Get_Lex_Hexdigit(&nibble, *cp)) {
        i = (i << 4) + nibble;
        cp++;
    }

    if (len < minlen)
        return nullptr;

    Init_Integer(out, i);
    return cp;
}


//
//  Try_Scan_Hex2: C
//
// Decode a %xx hex encoded sequence into a byte value.
//
// The % should already be removed before calling this.
//
// Returns new position after advancing or NULL.  On success, it always
// consumes two bytes (which are two codepoints).
//
Option(const Byte*) Try_Scan_Hex2(Byte* decoded_out, const Byte* bp)
{
    Byte nibble1;
    if (not Try_Get_Lex_Hexdigit(&nibble1, bp[0]))
        return nullptr;

    Byte nibble2;
    if (not Try_Get_Lex_Hexdigit(&nibble2, bp[1]))
        return nullptr;

    *decoded_out = cast(Codepoint, (nibble1 << 4) + nibble2);

    return bp + 2;
}


//
//  Try_Scan_Decimal_To_Stack: C
//
// Scan and convert a decimal value.  Return new character position or null.
//
Option(const Byte*) Try_Scan_Decimal_To_Stack(
    const Byte* cp,
    REBLEN len,
    bool dec_only
){
    Byte buf[MAX_NUM_LEN + 4];
    Byte* ep = buf;
    if (len > MAX_NUM_LEN)
        return nullptr;

    const Byte* bp = cp;

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
        return nullptr;

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
            return nullptr;
    }

    if (*cp == '%') {
        if (dec_only)
            return nullptr;

        ++cp; // ignore it
    }

    *ep = '\0';

    if (cp - bp != len)
        return nullptr;

    char *se;
    double d = strtod(s_cast(buf), &se);
    if (fabs(d) == HUGE_VAL)  // !!! TBD: need check for NaN, and INF
        fail (Error_Overflow_Raw());

    Init_Decimal(PUSH(), d);
    return cp;
}


//
//  Try_Scan_Integer_To_Stack: C
//
// Scan and convert an integer value.  Return new position or null if error.
// Allow preceding + - and any combination of ' marks.
//
const Byte* Try_Scan_Integer_To_Stack(
    const Byte* cp,
    REBLEN len
){
    if (len == 1 and Is_Lex_Number(*cp)) {  // fast convert single digit #s
        Init_Integer(PUSH(), Get_Lex_Number(*cp));
        return cp + 1;
    }

    Byte buf[MAX_NUM_LEN + 4];
    if (len > MAX_NUM_LEN)
        return nullptr;  // prevent buffer overflow

    Byte* bp = buf;

    bool neg = false;

    REBINT num = len;

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
        Init_Integer(PUSH(), 0);
        return cp;
    }

    // Copy all digits, except ' :
    for (; num > 0; num--) {
        if (*cp >= '0' && *cp <= '9')
            *bp++ = *cp++;
        else if (*cp == '\'')
            ++cp;
        else
            return nullptr;
    }
    *bp = '\0';

    // Too many digits?
    len = bp - &buf[0];
    if (neg)
        --len;
    if (len > 19) {
        // !!! magic number :-( How does it relate to MAX_INT_LEN (also magic)
        return nullptr;
    }

    // Convert, check, and return:
    errno = 0;

    REBI64 i = CHR_TO_INT(buf);
    if (errno != 0)
        return nullptr;  // overflow

    if ((i > 0 and neg) or (i < 0 and not neg))
        return nullptr;

    Init_Integer(PUSH(), i);
    return cp;
}


//
//  Try_Scan_Date_To_Stack: C
//
// Scan and convert a date. Also can include a time and zone.
//
Option(const Byte*) Try_Scan_Date_To_Stack(const Byte* cp, REBLEN len) {
    const Byte* end = cp + len;

    // Skip spaces:
    for (; *cp == ' ' && cp != end; cp++);

    // Skip day name, comma, and spaces:
    const Byte* ep;
    for (ep = cp; *ep != ',' && ep != end; ep++);
    if (ep != end) {
        cp = ep + 1;
        while (*cp == ' ' && cp != end) cp++;
    }
    if (cp == end)
        return nullptr;

    REBINT num;

    if (not (ep = maybe Try_Grab_Int(&num, cp)))  // Day or 4-digit year
        return nullptr;
    if (num < 0)
        return nullptr;

    REBINT day;
    REBINT month;
    REBINT year;
    REBINT tz = NO_DATE_ZONE;
    REBI64 nanoseconds = NO_DATE_TIME; // may be overwritten

    Size size = ep - cp;
    if (size >= 4) {
        // year is set in this branch (we know because day is 0)
        // Ex: 2009/04/20/19:00:00+0:00
        year = num;
        day = 0;
    }
    else {
        assert(size != 0);  // because Try_Grab_Int() succeeded

        // year is not set in this branch (we know because day ISN'T 0)
        // Ex: 12-Dec-2012
        day = num;
        if (day == 0)
            return nullptr;

        // !!! Clang static analyzer doesn't know from test of `day` below
        // how it connects with year being set or not.  Suppress warning.
        year = INT32_MIN; // !!! Garbage, should not be read.
    }

    cp = ep;

    // Determine field separator:
    if (*cp != '/' && *cp != '-' && *cp != '.' && *cp != ' ')
        return nullptr;

    Byte sep = *cp++;

    const Byte *ep_num = maybe Try_Grab_Int(&num, cp);

    if (ep_num) {  // month was a number
        if (num < 0)
            return nullptr;

        month = num;
        ep = ep_num;
    }
    else { // month must be a word
        for (ep = cp; Is_Lex_Word(*ep); ep++)
            NOOP; // scan word

        size = ep - cp;
        if (size < 3)
            return nullptr;

        for (num = 0; num != 12; ++num) {
            const Byte* month_name = cb_cast(Month_Names[num]);
            if (0 == Compare_Ascii_Uncased(month_name, cp, size))
                break;
        }
        month = num + 1;
    }

    if (month < 1 || month > 12)
        return nullptr;

    cp = ep;
    if (*cp++ != sep)
        return nullptr;

    ep = maybe Try_Grab_Int(&num, cp);  // Year or day (if year was first)
    if (not ep or *cp == '-' or num < 0)
        return nullptr;

    size = ep - cp;
    assert(size > 0);  // because Try_Grab_Int() succeeded

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
        return nullptr;

    // Check February for leap year or century:
    if (month == 2 && day == 29) {
        if (
            ((year % 4) != 0) ||        // not leap year
            ((year % 100) == 0 &&       // century?
            (year % 400) != 0)
        ){
            return nullptr;  // not leap century
        }
    }

    cp = ep;

    if (cp >= end)
        goto end_date;

    if (*cp == '/' || *cp == ' ') {
        sep = *cp++;

        if (cp >= end)
            goto end_date;

        Option(Length) time_len = 0;  // !!! not used/required by time scan?
        if (not (cp = maybe Try_Scan_Time_To_Stack(cp, time_len)))
            return nullptr;

        if (
            VAL_NANO(TOP) < 0
            or VAL_NANO(TOP) >= SECS_TO_NANO(24 * 60 * 60)
        ){
            return nullptr;
        }
        assert(PAYLOAD(Time, TOP).nanoseconds != NO_DATE_TIME);
        nanoseconds = VAL_NANO(TOP);
        DROP();  // !!! could reuse top cell for "efficiency"
    }

    // past this point, header is set, so `goto end_date` is legal.

    if (*cp == sep)
        ++cp;

    // Time zone can be 12:30 or 1230 (optional hour indicator)
    if (*cp == '-' || *cp == '+') {
        if (cp >= end)
            goto end_date;

        if (not (ep = maybe Try_Grab_Int(&num, cp + 1)))
            return nullptr;

        if (*ep != ':') {
            if (num < -1500 || num > 1500)
                return nullptr;

            int h = (num / 100);
            int m = (num - (h * 100));

            tz = (h * 60 + m) / ZONE_MINS;
        }
        else {
            if (num < -15 || num > 15)
                return nullptr;

            tz = num * (60 / ZONE_MINS);

            if (*ep == ':') {
                ep = maybe Try_Grab_Int(&num, ep + 1);
                if (not ep or num % ZONE_MINS != 0)
                    return nullptr;

                tz += num / ZONE_MINS;
            }
        }

        if (ep != end)
            return nullptr;

        if (*cp == '-')
            tz = -tz;

        cp = ep;
    }

  end_date:

    // Overwriting scanned REB_TIME...
    // payload.time.nanoseconds set
    // may be NO_DATE_TIME, don't Freshen_Cell_Header()
    //
    Reset_Cell_Header_Noquote(PUSH(), CELL_MASK_DATE);
    VAL_YEAR(TOP) = year;
    VAL_MONTH(TOP) = month;
    VAL_DAY(TOP) = day;
    VAL_ZONE(cast(Cell*, TOP)) = NO_DATE_ZONE;  // Adjust_Date_Zone() needs
    PAYLOAD(Time, TOP).nanoseconds = nanoseconds;

    Adjust_Date_Zone_Core(TOP, tz);

    VAL_ZONE(cast(Cell*, TOP)) = tz;

    return cp;
}


//
//  Try_Scan_File_To_Stack: C
//
// Scan and convert a file name.
//
Option(const Byte*) Try_Scan_File_To_Stack(const Byte* cp, REBLEN len)
{
    if (*cp == '%') {
        cp++;
        len--;
    }

    Codepoint term;
    const Byte* invalids;
    if (*cp == '"') {
        cp++;
        len--;
        term = '"';
        invalids = cb_cast(":;\"");
    }
    else {
        term = '\0';
        invalids = cb_cast(":;()[]\"");
    }

    DECLARE_MOLDER (mo);

    cp = maybe Try_Scan_Utf8_Item_Push_Mold(mo, cp, cp + len, term, invalids);
    if (cp == nullptr) {
        Drop_Mold(mo);
        return nullptr;
    }

    Init_File(PUSH(), Pop_Molded_String(mo));
    return cp;
}


//
//  Try_Scan_Email_To_Stack: C
//
// Scan and convert email.
//
Option(const Byte*) Try_Scan_Email_To_Stack(const Byte* cp, REBLEN len)
{
    String* s = Make_String(len * 2);  // !!! guess...use mold buffer instead?
    Utf8(*) up = String_Head(s);

    REBLEN num_chars = 0;

    bool found_at = false;
    for (; len > 0; len--) {
        if (*cp == '@') {
            if (found_at)
                return nullptr;
            found_at = true;
        }

        if (*cp == '%') {
            if (len <= 2)
                return nullptr;

            Byte decoded;
            if (not (cp = maybe Try_Scan_Hex2(&decoded, cp + 1)))
                return nullptr;

            up = Write_Codepoint(up, decoded);
            ++num_chars;
            len -= 2;
        }
        else {
            up = Write_Codepoint(up, *cp++);
            ++num_chars;
        }
    }

    if (not found_at)
        return nullptr;

    Term_String_Len_Size(s, num_chars, up - String_Head(s));

    PUSH();

    if (Try_Init_Small_Utf8(
        TOP,
        REB_EMAIL,
        String_Head(s),
        String_Len(s),
        String_Size(s)
    )){
        Free_Unmanaged_Flex(s);
        return cp;
    }

    Freeze_Flex(s);
    Init_Any_String(TOP, REB_EMAIL, s);
    return cp;
}


//
//  Try_Scan_URL_To_Stack: C
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
// converted as TEXT!, where their datatype suggests the encodedness:
//
//     {https://duckduckgo.com/?q=herg%C3%A9+%26+tintin}
//
// (This is similar to how local FILE!s, where e.g. slashes become backslash
// on Windows, are expressed as TEXT!.)
//
// 1. The code wasn't set up to do validation of URLs, it just assumed that
//    Prescan_Token() had done the necessary checking.  But this routine
//    is used in TO URL! conversion as well, where validation is necessary.
//    All this needs review after more fundamental questions shake out,
//    but for now we add a check.
//
Option(const Byte*) Try_Scan_URL_To_Stack(const Byte* cp, REBLEN len)
{
    const Byte* t = cp;
    const Byte* tail = cp + len;
    while (true) {
        if (t == tail)
            return nullptr;  // didn't find "://"

        if (*t != ':') {
            ++t;
            continue;
        }
        ++t;  // found :
        if (t == tail)
            return nullptr;
        if (*t == ':')   // log::foo style URL legal as well
            break;
        if (*t != '/')
            return nullptr;
        ++t;  // found :/
        if (t == tail or *t != '/')
            return nullptr;
        break;  // found ://
    }

    String* s = Append_UTF8_May_Fail(
        nullptr,
        cs_cast(cp),
        len,
        STRMODE_NO_CR
    );

    PUSH();

    if (Try_Init_Small_Utf8(
        TOP,
        REB_URL,
        String_Head(s),
        String_Len(s),
        String_Size(s)
    )){
        Free_Unmanaged_Flex(s);  // !!! direct mold buffer use would be better
        return cp + len;
    }

    Freeze_Flex(s);
    Init_Any_String(TOP, REB_URL, s);

    return cp + len;
}


//
//  Try_Scan_Pair_To_Stack: C
//
// Scan and convert a pair
//
Option(const Byte*) Try_Scan_Pair_To_Stack(
    const Byte* cp,
    REBLEN len
){
    const Byte* bp = cp;

    REBINT x;
    if (not (cp = maybe Try_Grab_Int(&x, cp)))
        return nullptr;
    if (*cp != 'x' and *cp != 'X')
        return nullptr;

    cp++;

    REBINT y;
    if (not (cp = maybe Try_Grab_Int(&y, cp)))
        return nullptr;

    if (len > cp - bp)  // !!! scanner checks if not precisely equal...
        return nullptr;

    Init_Pair(PUSH(), x, y);
    return cp;
}


//
//  Try_Scan_Binary_To_Stack: C
//
// Scan and convert binary strings.
//
Option(const Byte*) Try_Scan_Binary_To_Stack(
    const Byte* cp,
    REBLEN len
){
    REBINT base = 16;

    if (*cp != '#') {
        const Byte* ep = maybe Try_Grab_Int(&base, cp);
        if (not ep or *ep != '#')
            return nullptr;
        len -= ep - cp;
        cp = ep;
    }

    cp++;  // skip #
    if (*cp++ != '{')
        return nullptr;

    len -= 2;

    Binary* decoded = maybe Decode_Enbased_Utf8_As_Binary(&cp, len, base, '}');
    if (not decoded)
        return nullptr;

    cp = maybe Skip_To_Byte(cp, cp + len, '}');
    if (not cp) {
        Free_Unmanaged_Flex(decoded);
        return nullptr;
    }

    Init_Blob(PUSH(), decoded);

    return cp + 1;  // include the "}" in the scan total
}


//
//  /scan-net-header: native [
//
//  "Scan an Internet-style header (HTTP, SMTP)"
//
//      return: [block!]
//      header "Fields with duplicate words will be merged into a block"
//          [blob!]
//  ]
//
DECLARE_NATIVE(scan_net_header)
//
// !!! This routine used to be a feature of CONSTRUCT in R3-Alpha, and was
// used by %prot-http.r.  The idea was that instead of providing a parent
// object, a STRING! or BLOB! could be provided which would be turned
// into a block by this routine.
//
// It doesn't make much sense to have this coded in C rather than using PARSE
// It's only being converted into a native to avoid introducing bugs by
// rewriting it as Rebol in the middle of other changes.
{
    INCLUDE_PARAMS_OF_SCAN_NET_HEADER;

    Source* result = Make_Source_Managed(10);  // Guess at size (data stack?)

    Value* header = ARG(header);
    Size size;
    const Byte* cp = Cell_Bytes_At(&size, header);
    UNUSED(size);  // !!! Review semantics

    while (Is_Lex_Whitespace(*cp)) cp++; // skip white space

    const Byte* start;
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

        Cell* val = nullptr;  // suppress maybe uninitialized warning

        const Symbol* name = Intern_UTF8_Managed(start, cp - start);

        cp++;
        // Search if word already present:

        const Element* item_tail = Array_Tail(result);
        Element* item = Array_Head(result);

        for (; item != item_tail; item += 2) {
            assert(Is_Text(item + 1) || Is_Block(item + 1));
            if (Are_Synonyms(Cell_Word_Symbol(item), name)) {
                // Does it already use a block?
                if (Is_Block(item + 1)) {
                    // Block of values already exists:
                    val = Alloc_Tail_Array(Cell_Array_Ensure_Mutable(item + 1));
                }
                else {
                    // Create new block for values:
                    Source* a = Make_Source_Managed(2);
                    Derelativize(
                        Alloc_Tail_Array(a),
                        item + 1, // prior value
                        SPECIFIED // no relative values added
                    );
                    val = Alloc_Tail_Array(a);
                    Init_Block(item + 1, a);
                }
                break;
            }
        }

        if (item == item_tail) {  // didn't break, add space for new word/value
            Init_Set_Word(Alloc_Tail_Array(result), name);
            val = Alloc_Tail_Array(result);
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
        // correctly, it would need to use Utf8_Next to count the characters
        // in the loop above.  Better to convert to usermode.

        String* string = Make_String(len * 2);
        Utf8(*) str = String_Head(string);
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
        Term_String_Len_Size(string, len, str - String_Head(string));
        Init_Text(val, string);
    }

    return Init_Block(OUT, result);
}
