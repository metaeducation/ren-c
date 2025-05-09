//
//  file: %l-types.c
//  summary: "special lexical type converters"
//  section: lexical
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2025 Ren-C Open Source Contributors
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
//  type-of: native [
//
//  "Give back the type of a value (all quoted values return QUOTED!)"
//
//      return: "TYPE-OF null returns an error, use TRY if meant"
//          [datatype! error!]
//      value [any-value?]
//  ]
//
DECLARE_NATIVE(TYPE_OF)
{
    INCLUDE_PARAMS_OF_TYPE_OF;

    Value* v = ARG(VALUE);

    if (Is_Nulled(v))
        return FAIL(Error_Type_Of_Null_Raw());  // caller can TRY if meant

    return COPY(Datatype_Of(v));
}


//
//  heart-of: native [
//
//  "Give back a cell's heart (e.g. HEART OF ~FOO~ or ''FOO is WORD!)"
//
//      return: [~null~ datatype!]
//      element "Antiforms not accepted, use (heart of meta value) if needed"
//          [<opt-out> element?]
//  ]
//
DECLARE_NATIVE(HEART_OF)
{
    INCLUDE_PARAMS_OF_HEART_OF;

    Element* elem = Element_ARG(ELEMENT);

    Option(Heart) heart = Heart_Of(elem);
    if (heart)
        return COPY(Datatype_From_Type(unwrap heart));

    return PANIC("HEART OF not supported for extension types...yet!");
}


//
//  quotes-of: native [
//
//  "Return how many quote levels are on a value (quasiforms have 0 quotes)"
//
//      return: [~null~ integer!]
//      element [<opt-out> element?]
//  ]
//
DECLARE_NATIVE(QUOTES_OF)
{
    INCLUDE_PARAMS_OF_QUOTES_OF;

    return Init_Integer(OUT, Quotes_Of(Element_ARG(ELEMENT)));
}


//
//  sigil-of: native:generic [
//
//  "Get the SIGIL! on a value, e.g. $WORD has the $ sigil, WORD has none"
//
//      return: [~null~ sigil!]
//      element [<opt-out> fundamental?]
//  ]
//
DECLARE_NATIVE(SIGIL_OF)
{
    INCLUDE_PARAMS_OF_SIGIL_OF;

    Element* elem = Element_ARG(ELEMENT);

    Option(Sigil) sigil = Sigil_Of(elem);
    if (not sigil)
        return nullptr;
    return Init_Sigil(OUT, unwrap sigil);
}


//
//  length-of: native:generic [
//
//  "Get the length (in series units, e.g. codepoints) of series or other type"
//
//      return: [~null~ integer!]
//      element [<opt-out> fundamental?]  ; not quoted or quasi [1]
//  ]
//
DECLARE_NATIVE(LENGTH_OF)
//
// 1. See remarks on Dispatch_Generic() for why we don't allow things
//    like (3 = length of ''[a b c]).  An exception is made for action
//    antiforms, because they cannot be put in blocks, so their impact
//    is limited...and we want things like (label of append/) to be able
//    to work.  So only those are turned into the plain form here.
{
    INCLUDE_PARAMS_OF_LENGTH_OF;

    return Dispatch_Generic(LENGTH_OF, Element_ARG(ELEMENT), LEVEL);
}


//
//  size-of: native:generic [
//
//  "Get the size (in bytes, e.g. UTF-encoded bytes) of series or other type"
//
//      return: [~null~ integer!]
//      element [<opt-out> fundamental?]
//  ]
//
DECLARE_NATIVE(SIZE_OF)
//
// 1. The SIZE-OF native used to be distinct from the SIZE OF reflector, but
//    now that these things are unified the usermode SIZE-OF for checking the
//    size of a file! or url! would conflict.  There's not currently a way
//    to write generics in usermode, but that ability is scheduled.  Hack
//    it in just for now.
{
    INCLUDE_PARAMS_OF_SIZE_OF;

    Element* elem = Element_ARG(ELEMENT);

    if (Is_File(elem) or Is_Url(elem))  // !!! hack in FILE! and URL! [1]
        return rebDelegate(
            "all wrap [info: info?", elem, "info.size]"
        );

    return Dispatch_Generic(SIZE_OF, elem, LEVEL);
}


//
//  index-of: native:generic [
//
//  "Get the index of a series type"
//
//      return: [~null~ integer!]
//      element [<opt-out> fundamental?]
//  ]
//
DECLARE_NATIVE(INDEX_OF)
//
// !!! Should there be a generalized error catch all for ANY-ELEMENT? which
// says `return FAIL(Error_Type_Has_No_Index_Raw(Datatype_Of(elem)));`?  Review.
{
    INCLUDE_PARAMS_OF_INDEX_OF;

    return Dispatch_Generic(INDEX_OF, Element_ARG(ELEMENT), LEVEL);
}


//
//  offset-of: native:generic [
//
//  "Get the offset of a series type or port (zero-based?)"
//
//      return: [~null~ integer!]
//      element [<opt-out> fundamental?]
//  ]
//
DECLARE_NATIVE(OFFSET_OF)
{
    INCLUDE_PARAMS_OF_OFFSET_OF;

    return Dispatch_Generic(OFFSET_OF, Element_ARG(ELEMENT), LEVEL);
}


//
//  address-of: native:generic [
//
//  "Get the memory address of a type's data (low-level, beware!)"
//
//      return: [~null~ integer!]
//      element [<opt-out> <unrun> fundamental?]
//  ]
//
DECLARE_NATIVE(ADDRESS_OF)
//
// !!! This really needs to lock types, so that the memory can't move.  But
// for now it's a hack just to get the FFI able to find out the buffer
// behind a VECTOR!, etc.
{
    INCLUDE_PARAMS_OF_ADDRESS_OF;

    Element* elem = Element_ARG(ELEMENT);

    return Dispatch_Generic(ADDRESS_OF, elem, LEVEL);
}


// Asking for the ADDRESS OF a FRAME! delegates that to the DetailsQuerier.
//
// !!! It's an open question of whether functions will use the new extended
// types system to add types for ROUTINE! and ENCLOSURE! and ADAPTATION! etc.
// such that the DetailsQuerier would be obsolete.
//
IMPLEMENT_GENERIC(ADDRESS_OF, Is_Frame)
{
    INCLUDE_PARAMS_OF_ADDRESS_OF;

    Element* frame = Element_ARG(ELEMENT);

    Phase* phase = Cell_Frame_Phase(frame);
    if (not Is_Stub_Details(phase))
        return PANIC("Phase isn't details, can't get ADDRESS-OF");

    Details* details = cast(Details*, phase);
    DetailsQuerier* querier = Details_Querier(details);
    if (not (*querier)(OUT, details, SYM_ADDRESS_OF))
        return FAIL(
            "Frame Details does not offer ADDRESS-OF, use TRY for NULL"
        );

    return OUT;
}


//
//  of: infix native [
//
//  "Call XXX-OF functions without a hyphen, e.g. HEAD OF X => HEAD-OF X"
//
//      return: [any-value?]
//      @(property) "Escapable slot for WORD!"
//          [word!]
//  ]
//
DECLARE_NATIVE(OF)
{
    INCLUDE_PARAMS_OF_OF;

    enum {
        ST_OF_INITIAL_ENTRY = STATE_0,
        ST_OF_REEVALUATING
    };

    switch (STATE) {
      case ST_OF_INITIAL_ENTRY:
        goto initial_entry;

      case ST_OF_REEVALUATING:  // stepper gives a meta-result
        return Meta_Unquotify_Undecayed(OUT);

      default: assert(false);
    }

  initial_entry: { /////////////////////////////////////////////////////////

    Element* prop = Element_ARG(PROPERTY);
    assert(Is_Word(prop));
    const Symbol* sym = Cell_Word_Symbol(prop);
    const Symbol* sym_of;

    Option(SymId) opt_id = Symbol_Id(sym);
    if (opt_id and cast(SymId16, opt_id) <= MAX_SYM_BUILTIN - 1)
        goto check_if_next_is_sym_of;

    goto no_optimization;

  check_if_next_is_sym_of: { /////////////////////////////////////////////////

    // In order to speed up the navigation from builtin symbols like HEAD to
    // find HEAD-OF, the %make-boot.r process attempts to reorder the symbols
    // in such a way that HEAD-OF is the SymId immediately after HEAD.
    //
    // This can't always be done.  e.g. if the symbol is in an order-dependent
    // range, such as datatypes: SIGIL can't be followed by SIGIL-OF because
    // SIGIL-OF is not a datatype.  It also obviously won't work for an -OF
    // function that the user (or unprocessed Mezzanine) comes up with after
    // the fact.  So we check to see if the next symbol is an -OF match and
    // save on symbol hashing and lookup.
    //
    // (This could be optimized at load time if symbols carried a flag that
    // said the next symbol was an -OF, but rather than take a symbol flag
    // for now we just do the relatively cheap check.)

    SymId id = unwrap opt_id;

    const Byte* utf8 = String_Head(Canon_Symbol(id));
    SymId next_id = cast(SymId, cast(int, id) + 1);
    const Byte* maybe_utf8_of = String_Head(Canon_Symbol(next_id));
    while (true) {
        if (*maybe_utf8_of == '\0')  // hit end of what would be "longer"
            goto no_optimization;
        if (*utf8 == '\0')  // hit end of what would be "shorter"...
            break;  // might be a match if "-of" are next 3 chars!
        if (*utf8 != *maybe_utf8_of)  // mismatch before end of shorter
            goto no_optimization;
        ++utf8;
        ++maybe_utf8_of;
    }
    if (
        maybe_utf8_of[0] == '-'
        and maybe_utf8_of[1] == 'o'
        and maybe_utf8_of[2] == 'f'
        and maybe_utf8_of[3] == '\0'
    ){
        sym_of = Canon_Symbol(next_id);
        goto have_sym_of;
    }

} no_optimization: { /////////////////////////////////////////////////////////

    Byte buffer[256];
    Size size = String_Size(sym);
    Mem_Copy(buffer, String_UTF8(sym), size);
    buffer[size] = '-';
    ++size;
    buffer[size] = 'o';
    ++size;
    buffer[size] = 'f';
    ++size;
    sym_of = Intern_UTF8_Managed(buffer, size);

} have_sym_of: { /////////////////////////////////////////////////////////////

    Element* prop_of = Init_Word(SPARE, sym_of);

    const Value* fetched;
    Option(Error*) e = Trap_Lookup_Word(
        &fetched,
        prop_of,
        Feed_Binding(LEVEL->feed)
    );
    if (e)
        return PANIC(unwrap e);

    if (not Is_Action(fetched))
        return PANIC("OF looked up to a value that wasn't an ACTION!");

    Flags flags = FLAG_STATE_BYTE(ST_STEPPER_REEVALUATING)
        | LEVEL_FLAG_ERROR_RESULT_OK;

    Level* sub = Make_Level(&Meta_Stepper_Executor, level_->feed, flags);
    Copy_Meta_Cell(Evaluator_Level_Current(sub), fetched);
    QUOTE_BYTE(Evaluator_Level_Current(sub)) = NOQUOTE_1;  // plain FRAME!
    sub->u.eval.current_gotten = nullptr;

    Push_Level_Erase_Out_If_State_0(OUT, sub);

    STATE = ST_OF_REEVALUATING;
    return CONTINUE_SUBLEVEL(sub);  // !!! could/should we replace this level?
}}}


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
        panic (Error_Overflow_Raw());

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
            const Byte* month_name = cb_cast(g_month_names[num]);
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

    if (year > MAX_YEAR || day < 1 || day > g_month_max_days[month-1])
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
        assert(TOP->payload.nanoseconds != NO_DATE_TIME);
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

    // Overwriting scanned TYPE_TIME...
    // payload.time.nanoseconds set
    // may be NO_DATE_TIME, don't Freshen_Cell_Header()
    //
    Reset_Cell_Header_Noquote(PUSH(), CELL_MASK_DATE);
    VAL_YEAR(TOP) = year;
    VAL_MONTH(TOP) = month;
    VAL_DAY(TOP) = day;
    VAL_ZONE(cast(Cell*, TOP)) = NO_DATE_ZONE;  // Adjust_Date_Zone() needs
    Tweak_Cell_Nanoseconds(TOP, nanoseconds);

    Adjust_Date_Zone_Core(TOP, tz);

    VAL_ZONE(cast(Cell*, TOP)) = tz;

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
        TYPE_EMAIL,
        String_Head(s),
        String_Len(s),
        String_Size(s)
    )){
        Free_Unmanaged_Flex(s);
        return cp;
    }

    Freeze_Flex(s);
    Init_Any_String(TOP, TYPE_EMAIL, s);
    return cp;
}


//
//  Try_Scan_Money_To_Stack: C
//
// MONEY! in historical Rebol was numeric with an abandoned and esoteric
// implementation.  Ren-C makes it an ANY-UTF8? type instead, giving it the
// ability to hold a string of any length.  If you want to do math on it, you
// have to convert it to a numeric type.  (The original "deci" implementation
// for MONEY! is now a separate type available in an extension as DECI! for
// those who want it, but it's not part of the core distribution.)
//
// 1. It's conceivable that we could broaden the type to allow for more
//    than just digits and two decimal places.  But there are diminishing
//    returns, and more benefit to keeping it tame.  The most interesting
//    use for dialecting is simply as integers, e.g. $1 $2 $3 $4 $5 for
//    locating substitution points.
//
Option(const Byte*) Try_Scan_Money_To_Stack(const Byte* cp, REBLEN len)
{
    String* s = Make_String(len);  // only ASCII allowed, "1"-"9" and "."
    Utf8(*) up = String_Head(s);

    assert(*cp == '$');
    ++cp;
    --len;

    if (*cp == '-' or *cp == '+') {  // -$1.00 no longer legal, use $-1.00
        up = Write_Codepoint(up, *cp);
        ++cp;
        --len;
    }

    uint_fast8_t dot_and_digits_len = 0;

    REBLEN n;
    for (n = 0; n < len; ++n, ++cp) {
        if (*cp == '.') {
            if (dot_and_digits_len != 0)
                return nullptr;  // don't allow $10.00.0, etc [1]

            dot_and_digits_len = 1;  // need exactly two more...
        }
        else if (Is_Lex_Number(*cp)) {
            if (dot_and_digits_len != 0)
                ++dot_and_digits_len;
        }
        else
            return nullptr;

        up = Write_Codepoint(up, *cp);
    }

    if (dot_and_digits_len != 0 and dot_and_digits_len != 3)
        return nullptr;  // Only allow 2 digits after the dot, if present [1]

    Term_String_Len_Size(s, len, up - String_Head(s));

    PUSH();

    if (Try_Init_Small_Utf8(
        TOP,
        TYPE_MONEY,
        String_Head(s),
        String_Len(s),
        String_Size(s)
    )){
        Free_Unmanaged_Flex(s);
        return cp;
    }

    Freeze_Flex(s);
    Init_Any_String(TOP, TYPE_MONEY, s);
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

    String* s = Append_UTF8_May_Panic(
        nullptr,
        cs_cast(cp),
        len,
        STRMODE_NO_CR
    );

    PUSH();

    if (Try_Init_Small_Utf8(
        TOP,
        TYPE_URL,
        String_Head(s),
        String_Len(s),
        String_Size(s)
    )){
        Free_Unmanaged_Flex(s);  // !!! direct mold buffer use would be better
        return cp + len;
    }

    Freeze_Flex(s);
    Init_Any_String(TOP, TYPE_URL, s);

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
//  scan-net-header: native [
//
//  "Scan an Internet-style header (HTTP, SMTP)"
//
//      return: [block!]
//      header "Fields with duplicate words will be merged into a block"
//          [blob!]
//  ]
//
DECLARE_NATIVE(SCAN_NET_HEADER)
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

    Value* header = ARG(HEADER);
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
