//
//  File: %n-strings.c
//  Summary: "native functions for strings"
//  Section: natives
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


static bool Check_Char_Range(const Value* val, Codepoint limit)
{
    if (IS_CHAR(val))
        return Cell_Codepoint(val) <= limit;

    if (Is_Integer(val))
        return VAL_INT64(val) <= cast(REBI64, limit);

    assert(Any_String(val));

    REBLEN len;
    Utf8(const*) up = Cell_Utf8_Len_Size_At(&len, nullptr, val);

    for (; len > 0; len--) {
        Codepoint c;
        up = Utf8_Next(&c, up);

        if (c > limit)
            return false;
    }

    return true;
}


//
//  /ascii?: native [
//
//  "Returns TRUE if value or string is in ASCII character range (below 128)"
//
//      return: [logic?]
//      value [any-string? char? integer!]
//  ]
//
DECLARE_NATIVE(ascii_q)
{
    INCLUDE_PARAMS_OF_ASCII_Q;

    return Init_Logic(OUT, Check_Char_Range(ARG(value), 0x7f));
}


//
//  /latin1?: native [
//
//  "Returns TRUE if value or string is in Latin-1 character range (below 256)"
//
//      return: [logic?]
//      value [any-string? char? integer!]
//  ]
//
DECLARE_NATIVE(latin1_q)
{
    INCLUDE_PARAMS_OF_LATIN1_Q;

    return Init_Logic(OUT, Check_Char_Range(ARG(value), 0xff));
}


#define LEVEL_FLAG_DELIMIT_MOLD_RESULT  LEVEL_FLAG_MISCELLANEOUS

#define nothing_delimited                   Not_Cell_Erased(OUT)  // or NULL
#define Mark_Delimit_Produced_Something()   Erase_Cell(OUT)

#define CELL_FLAG_DELIMITER_NOTE_PENDING    CELL_FLAG_NOTE


//
//  /delimit: native [
//
//  "Joins a block of values into TEXT! with delimiters"
//
//      return: "Null if blank input or block's contents are all null"
//          [~null~ text!]
//      delimiter [~null~ blank! char? text!]
//      line "Will be copied if already a text value"
//          [<maybe> text! block! the-block! issue!]
//      :head "Include delimiter at head of a non-NULL result"
//      :tail "Include delimiter at tail of a non-NULL result"
//  ]
//
DECLARE_NATIVE(delimit)
//
// Evaluates each item in a block and forms it, with an optional delimiter.
// If all the items in the block vaporize, or no items are found, this will
// return a nulled value.
{
    INCLUDE_PARAMS_OF_DELIMIT;

    Element* line = cast(Element*, ARG(line));
    Value* mold_handle = ARG(return);
    Molder* mo;  // sometimes uses handle stored in ARG(return), sometimes not

    Value* delimiter = ARG(delimiter);
    possibly(Get_Cell_Flag(delimiter, DELIMITER_NOTE_PENDING));

    enum {
        ST_DELIMIT_INITIAL_ENTRY = STATE_0,
        ST_DELIMIT_INITIALIZED_MOLDER,  // need to enable dispatcher catching
        ST_DELIMIT_STEPPING,
        ST_DELIMIT_EVALUATING_THE_GROUP
    };

    if (Is_Throwing(LEVEL)) {  // must clean up allocated Molder
        mo = Cell_Handle_Pointer(Molder, mold_handle);
        goto threw;
    }

    switch (STATE) {
      case ST_DELIMIT_INITIAL_ENTRY: {
        if (Is_Block(line) or Is_The_Block(line))
            goto delimit_block_initial_entry;

        goto simple_delimit_initial_entry; }

      case ST_DELIMIT_STEPPING:
        mo = Cell_Handle_Pointer(Molder, mold_handle);
        assert(Not_Level_Flag(LEVEL, DELIMIT_MOLD_RESULT));
        goto delimit_result_in_spare;

      case ST_DELIMIT_EVALUATING_THE_GROUP:
        mo = Cell_Handle_Pointer(Molder, mold_handle);
        if (Is_The_Block(ARG(line)))
            SUBLEVEL->executor = &Inert_Stepper_Executor;
        else {
            assert(Is_Block(line));
            SUBLEVEL->executor = &Stepper_Executor;
        }
        assert(Get_Level_Flag(LEVEL, DELIMIT_MOLD_RESULT));
        goto delimit_result_in_spare;

      default: assert(false);
    }

  simple_delimit_initial_entry: { ////////////////////////////////////////////

    // 1. Hard to unify this mold with code below that uses a level due to
    //    asserts on states balancing.  Easiest to repeat a small bit of code!

    assert(Is_Text(line) or Is_Issue(line));  // shortcut, no evals needed [1]

    Molder molder_struct;
    mo = &molder_struct;
    Construct_Molder(mo);
    Push_Mold(mo);

    if (REF(head) and not Is_Nulled(delimiter))
        Form_Element(mo, cast(Element*, delimiter));

    Form_Element(mo, line);

    if (REF(tail) and not Is_Nulled(delimiter))
        Form_Element(mo, cast(Element*, delimiter));

    return Init_Text(OUT, Pop_Molded_String(mo));

} delimit_block_initial_entry: { /////////////////////////////////////////////

    mo = Try_Alloc_Memory(Molder);
    Construct_Molder(mo);
    Init_Handle_Cdata(mold_handle, mo, 1);
    STATE = ST_DELIMIT_INITIALIZED_MOLDER;  // can't be STATE_0 if catch enable
    Enable_Dispatcher_Catching_Of_Throws(LEVEL);  // have to free Molder

    Push_Mold(mo);

    Executor* executor;
    if (Is_The_Block(ARG(line)))
        executor = &Inert_Stepper_Executor;
    else {
        assert(Is_Block(line));
        executor = &Stepper_Executor;
    }

    Level* sub = Make_Level_At(executor, line, LEVEL_FLAG_TRAMPOLINE_KEEPALIVE);
    Push_Level_Erase_Out_If_State_0(SPARE, sub);

    assert(Not_Cell_Flag(delimiter, DELIMITER_NOTE_PENDING));

    Init_Nulled(OUT);  // all elements seen so far void (erased if not true)
    assert(nothing_delimited);

    if (REF(head) and not Is_Nulled(delimiter))  // speculatively start with
        Form_Element(mo, cast(Element*, delimiter));  // may be tossed

    goto first_delimit_step;

} next_delimit_step: { ///////////////////////////////////////////////////////

    Reset_Evaluator_Erase_Out(SUBLEVEL);
    Clear_Level_Flag(LEVEL, DELIMIT_MOLD_RESULT);

} first_delimit_step: { //////////////////////////////////////////////////////

    Level* sub = SUBLEVEL;

    // 1. There's a concept that being able to put undelimited portions in the
    //    delimit is useful--and it really is:
    //
    //       >> print ["Outer" "spaced" ["inner" "unspaced"] "is" "useful"]
    //       Outer spaced innerunspaced is useful
    //
    //    Hacked in for the moment, but this routine should be reformulated
    //    to make it part of one continuous mold.
    //
    // 2. Blanks at source-level count as spaces (deemed too potentially broken
    //    to fetch them from variables and have them mean space).  This is
    //    a long-running experiment that may not pan out, but is cool enough to
    //    keep weighing the pros/cons.  Looked-up-to blanks are illegal.

    if (Is_Level_At_End(sub))
        goto finished;

    const Element* item = At_Level(sub);
    if (Is_Block(item) and not Is_Nulled(delimiter)) {  // hack [1]
        Derelativize(SPARE, item, Level_Binding(sub));
        Fetch_Next_In_Feed(sub->feed);

        Value* unspaced = rebValue(Canon(UNSPACED), rebQ(SPARE));
        if (unspaced == nullptr)  // vaporized, allow it
            goto next_delimit_step;

        Copy_Cell(SPARE, unspaced);
        rebRelease(unspaced);
        goto delimit_result_in_spare;
    }

    if (Is_Blank(item)) {  // BLANK! acts as space [2]
        Append_Codepoint(mo->string, ' ');
        Clear_Cell_Flag(delimiter, DELIMITER_NOTE_PENDING);
        Mark_Delimit_Produced_Something();
        Fetch_Next_In_Feed(sub->feed);
        goto next_delimit_step;
    }

    if (Any_The_Value(item)) {  // fetch and mold
        Set_Level_Flag(LEVEL, DELIMIT_MOLD_RESULT);

        if (Is_The_Word(item) or Is_The_Tuple(item)) {
            Get_Var_May_Fail(SPARE, item, Level_Binding(sub));
            Fetch_Next_In_Feed(sub->feed);
            goto delimit_result_in_spare;
        }

        if (Is_The_Group(item)) {
            STATE = ST_DELIMIT_EVALUATING_THE_GROUP;
            SUBLEVEL->executor = &Just_Use_Out_Executor;
            Derelativize(SCRATCH, item, Level_Binding(sub));
            HEART_BYTE(SCRATCH) = REB_BLOCK;  // the-block is different
            Fetch_Next_In_Feed(sub->feed);
            return CONTINUE(SPARE, cast(Element*, SCRATCH));
        }

        return FAIL(item);
    }

    if (Is_Quoted(item)) {  // just mold it
        Copy_Cell(SPARE, item);
        Set_Level_Flag(LEVEL, DELIMIT_MOLD_RESULT);

        Fetch_Next_In_Feed(sub->feed);
        goto delimit_result_in_spare;
    }

    STATE = ST_DELIMIT_STEPPING;
    return CONTINUE_SUBLEVEL(sub);  // just evaluate it

} delimit_result_in_spare: { /////////////////////////////////////////////////

    bool mold = Get_Level_Flag(LEVEL, DELIMIT_MOLD_RESULT);

    // 3. Erroring on NULL has been found to catch real bugs in practice.  It
    //    also enables clever constructs like CURTAIL.
    //
    // 4. BLOCK!s are prohibitied in DELIMIT because it's too often the case
    //    the result is gibberish--guessing what to do is bad:
    //
    //        >> block: [1 2 <x> hello]
    //
    //        >> print ["Your block is:" block]
    //        Your block is: 12<x>hello  ; ugh.
    //
    // 5. CHAR! suppresses the delimiter logic.  Hence:
    //
    //        >> delimit ":" ["a" space "b" newline void "c" newline "d" "e"]
    //        == "a b^/c^/d:e"
    //
    //    Only the last interstitial is considered a candidate for delimiting.
    //
    // 6. Empty strings distinct from voids in terms of still being delimited.
    //    This is important, e.g. in comma-delimited formats for empty fields.
    //
    //        >> delimit "," [field1 field2 field3]  ; field2 is ""
    //        one,,three
    //
    //    The same principle would apply to a "space-delimited format".

    if (Is_Elision(SPARE))  // spaced [elide print "hi"], etc
        goto next_delimit_step;  // vaporize

    Decay_If_Unstable(SPARE);  // spaced [match [logic?] false ...]

    if (Is_Void(SPARE))  // spaced [maybe null], spaced [if null [<a>]], etc
        goto next_delimit_step;  // vaporize

    if (Is_Nulled(SPARE))  // catches bugs in practice [3]
        return RAISE(Error_Need_Non_Null_Raw());

    if (Is_Splice(SPARE) and mold) {  // only allow splice for mold, for now
        if (Cell_Series_Len_At(SPARE) == 0)
            goto next_delimit_step;  // vaporize
    }
    else if (Is_Antiform(SPARE))
        return RAISE(Error_Bad_Antiform(SPARE));

    if (not mold) {
        if (Any_List(SPARE))  // guessing a behavior is bad [4]
            return FAIL("DELIMIT requires @var to mold lists");

        if (Any_Sequence(SPARE))  // can have lists in them, dicey [4]
            return FAIL("DELIMIT requires @var to mold sequences");

        if (Sigil_Of(cast(Element*, SPARE)))
            return FAIL("DELIMIT requires @var for elements with sigils");

        if (Is_Blank(SPARE))
            return FAIL("DELIMIT only treats source-level BLANK! as space");
    }

    Mark_Delimit_Produced_Something();

    if (Is_Issue(SPARE)) {  // do not delimit (unified w/char) [5]
        Form_Element(mo, cast(Element*, SPARE));
        Clear_Cell_Flag(delimiter, DELIMITER_NOTE_PENDING);
        goto next_delimit_step;
    }

    if (
        not Is_Nulled(delimiter)
        and Get_Cell_Flag(delimiter, DELIMITER_NOTE_PENDING)
    ){
        Form_Element(mo, cast(Element*, delimiter));
    }

    if (mold) {
        if (Is_Splice(SPARE)) {
            SET_MOLD_FLAG(mo, MOLD_FLAG_SPREAD);
            Mold_Or_Form_Cell_Ignore_Quotes(mo, SPARE, false);
            CLEAR_MOLD_FLAG(mo, MOLD_FLAG_SPREAD);
        }
        else
            Mold_Element(mo, cast(Element*, SPARE));
    }
    else {
        Form_Element(mo, cast(Element*, SPARE));
    }

    Set_Cell_Flag(delimiter, DELIMITER_NOTE_PENDING);  // empty strings too [6]

    goto next_delimit_step;

} finished: { ////////////////////////////////////////////////////////////////

    if (nothing_delimited) {
        assert(Is_Nulled(OUT));  // this is the signal for nothing delimited
        Drop_Mold(mo);
    }
    else {
        assert(Is_Cell_Erased(OUT));  // signal for something delimited

        if (REF(tail) and not Is_Nulled(delimiter))
            Form_Element(mo, cast(Element*, delimiter));
        Init_Text(OUT, Pop_Molded_String(mo));
    }

    Drop_Level(SUBLEVEL);
    Free_Memory(Molder, mo);
    return OUT;

} threw: { ///////////////////////////////////////////////////////////////////

    Drop_Mold(mo);
    Free_Memory(Molder, mo);
    unnecessary(Drop_Level(SUBLEVEL));  // automatically dropped on throws
    return THROWN;
}}


//
//  /debase: native [
//
//  "Decodes base-coded string (BASE-64 default) to binary value"
//
//      return: [blob!]
//      value [blob! text!]
//      :base "The base to convert from: 64, 16, or 2 (defaults to 64)"
//          [integer!]
//  ]
//
DECLARE_NATIVE(debase)
{
    INCLUDE_PARAMS_OF_DEBASE;

    Size size;
    const Byte* bp = Cell_Bytes_At(&size, ARG(value));

    REBINT base = 64;
    if (REF(base))
        base = VAL_INT32(ARG(base));
    else
        base = 64;

    Binary* decoded = maybe Decode_Enbased_Utf8_As_Binary(&bp, size, base, 0);
    if (not decoded)
        return FAIL(Error_Invalid_Data_Raw(ARG(value)));

    return Init_Blob(OUT, decoded);
}


//
//  /enbase: native [
//
//  "Encodes data into a binary, hexadecimal, or base-64 ASCII string"
//
//      return: [text!]
//      value "If text, will be UTF-8 encoded"
//          [blob! text!]
//      :base "Binary base to use: 64, 16, or 2 (BASE-64 default)"
//          [integer!]
//  ]
//
DECLARE_NATIVE(enbase)
{
    INCLUDE_PARAMS_OF_ENBASE;

    REBINT base;
    if (REF(base))
        base = VAL_INT32(ARG(base));
    else
        base = 64;

    Size size;
    const Byte* bp = Cell_Bytes_At(&size, ARG(value));

    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    const bool brk = false;
    switch (base) {
      case 64:
        Form_Base64(mo, bp, size, brk);
        break;

      case 16:
        Form_Base16(mo, bp, size, brk);
        break;

      case 2:
        Form_Base2(mo, bp, size, brk);
        break;

      default:
        return FAIL(PARAM(base));
    }

    return Init_Text(OUT, Pop_Molded_String(mo));
}


//
//  /enhex: native [
//
//  "Converts string to use URL-style hex encoding (%XX)"
//
//      return: "See http://en.wikipedia.org/wiki/Percent-encoding"
//          [any-string?]
//      string "String to encode, all non-ASCII or illegal URL bytes encoded"
//          [any-string?]
//  ]
//
DECLARE_NATIVE(enhex)
//
// 1. !!! Length 4 should be legal here, but a warning in an older GCC is
//    complaining that Encode_UTF8_Char reaches out of array bounds when it
//    does not appear to.  Possibly related to this:
//
//      https://gcc.gnu.org/bugzilla/show_bug.cgi?id=43949
//
// 2. Use uppercase hex digits, per RFC 3896 2.1, which is also consistent
//    with JavaScript's encodeURIComponent()
//
//      https://tools.ietf.org/html/rfc3986#section-2.1
//
//    !!! Should this be controlled by a :RELAX refinement and default to
//    not accepting lowercase?
{
    INCLUDE_PARAMS_OF_ENHEX;

    DECLARE_MOLDER (mo);
    Push_Mold (mo);

    REBLEN len;
    Utf8(const*) cp = Cell_Utf8_Len_Size_At(&len, nullptr, ARG(string));

    Codepoint c;
    cp = Utf8_Next(&c, cp);

    REBLEN i;
    for (i = 0; i < len; cp = Utf8_Next(&c, cp), ++i) {
        Byte encoded[UNI_ENCODED_MAX];
        REBLEN encoded_size;

        if (c >= 0x80) {  // all non-ASCII characters *must* be percent encoded
            encoded_size = Encoded_Size_For_Codepoint(c);
            Encode_UTF8_Char(encoded, c, encoded_size);
        }
        else {
            if (not Ascii_Char_Needs_Percent_Encoding(cast(Byte, c))) {
                Append_Codepoint(mo->string, c);
                continue;
            }
            encoded[0] = cast(Byte, c);
            encoded_size = 1;
        }

        REBLEN n;
        for (n = 0; n != encoded_size; ++n) {  // use uppercase hex digits [2]
            Append_Codepoint(mo->string, '%');
            Append_Codepoint(mo->string, Hex_Digits[(encoded[n] & 0xf0) >> 4]);
            Append_Codepoint(mo->string, Hex_Digits[encoded[n] & 0xf]);
        }
    }

    return Init_Any_String(
        OUT,
        Cell_Heart_Ensure_Noquote(ARG(string)),
        Pop_Molded_String(mo)
    );
}


//
//  /dehex: native [
//
//  "Converts URL-style encoded strings, %XX is interpreted as UTF-8 byte"
//
//      return: "Decoded string, with the same string type as the input"
//          [any-string?]
//      string "See http://en.wikipedia.org/wiki/Percent-encoding"
//          [any-string?]
//      :blob "Give result as a binary BLOB!, permits %00 encodings"  ; [1]
//  ]
//
DECLARE_NATIVE(dehex)
//
// 1. Ren-C is committed to having string types not contain the 0 codepoint,
//    but it's explicitly legal for percent encoding to allow %00 in URLs.
//    Sounds dangerous, but we can support that by returning a BLOB!.  The
//    code was written to use the mold buffer, however, and would have to
//    be rewritten to use a byte buffer for that feature.
{
    INCLUDE_PARAMS_OF_DEHEX;

    if (REF(blob))
        return FAIL("DEHEX:BLOB not yet implemented, but will permit %00");

    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    Utf8(const*) cp = Cell_Utf8_Head(ARG(string));

    Codepoint c;
    cp = Utf8_Next(&c, cp);

    while (c != '\0') {
        if (c != '%') {
            Append_Codepoint(mo->string, c);
            cp = Utf8_Next(&c, cp); // c may be '\0', guaranteed if `i == len`
            continue;
        }

        Byte scan[5];  // 4 bytes plus terminator is max, see RFC 3986
        Size scan_size = 0;

        do {
            if (scan_size > 4)
                return RAISE("Percent sequence over 4 bytes long (bad UTF-8)");

            Codepoint hex1;
            Codepoint hex2;
            cp = Utf8_Next(&hex1, cp);
            if (hex1 == '\0')
                hex2 = '\0';
            else
                cp = Utf8_Next(&hex2, cp);

            Byte nibble1;
            Byte nibble2;
            if (
                hex1 > UINT8_MAX
                or not Try_Get_Lex_Hexdigit(&nibble1, cast(Byte, hex1))
                or hex2 > UINT8_MAX
                or not Try_Get_Lex_Hexdigit(&nibble2, cast(Byte, hex2))
            ){
                return RAISE("2 hex digits must follow percent, e.g. %XX");
            }

            Byte b = (nibble1 << 4) + nibble2;

            if (scan_size == 0 and Is_Continuation_Byte(b))
                return RAISE("UTF-8 can't start with continuation byte");

            if (scan_size > 0 and not Is_Continuation_Byte(b)) {  // next char
                cp = Step_Back_Codepoint(cp);
                cp = Step_Back_Codepoint(cp);
                cp = Step_Back_Codepoint(cp);
                assert(*c_cast(Byte*, cp) == '%');
                break;
            }

            scan[scan_size] = b;
            ++scan_size;

            cp = Utf8_Next(&c, cp);

            if (b < 0x80)
                break;
        } while (c == '%');

        scan[scan_size] = '\0';

        const Byte* next = scan;
        Codepoint decoded;
        Option(Error*) e = Trap_Back_Scan_Utf8_Char(
            &decoded, &next, &scan_size
        );
        if (e)
            return RAISE(unwrap e);

        --scan_size;  // see definition of Back_Scan for why it's off by one
        if (scan_size != 0)
            return RAISE("Extra continuation characters in %XX of dehex");

        Append_Codepoint(mo->string, decoded);
    }

    return Init_Any_String(
        OUT,
        Cell_Heart_Ensure_Noquote(ARG(string)),
        Pop_Molded_String(mo)
    );
}


//
//  /deline: native [
//
//  "Converts string terminators to standard format, e.g. CR LF to LF"
//
//      return: [text! block!]
//      input "Will be modified (unless :LINES used)"
//          [text! blob!]
//      :lines "Return block of lines (works for LF, CR-LF endings)"
//  ]
//
DECLARE_NATIVE(deline)
{
    INCLUDE_PARAMS_OF_DELINE;

    // AS TEXT! verifies the UTF-8 validity of a BLOB!, and checks for any
    // embedded '\0' bytes, illegal in texts...without copying the input.
    //
    Value* input = rebValue("as text!", ARG(input));

    if (REF(lines)) {
        Init_Block(OUT, Split_Lines(cast(Element*, input)));
        rebRelease(input);
        return OUT;
    }

    String* s = Cell_String_Ensure_Mutable(input);
    REBLEN len_head = String_Len(s);

    REBLEN len_at = Cell_Series_Len_At(input);

    Utf8(*) dest = Cell_String_At_Known_Mutable(input);
    Utf8(const*) src = dest;

    // DELINE tolerates either LF or CR LF, in order to avoid disincentivizing
    // remote data in CR LF format from being "fixed" to pure LF format, for
    // fear of breaking someone else's script.  However, files must be in
    // *all* CR LF or *all* LF format.  If they are mixed they are considered
    // to be malformed...and need custom handling.
    //
    bool seen_a_cr_lf = false;
    bool seen_a_lone_lf = false;

    REBLEN n;
    for (n = 0; n < len_at;) {
        Codepoint c;
        src = Utf8_Next(&c, src);
        ++n;
        if (c == LF) {
            if (seen_a_cr_lf)
                return FAIL(Error_Mixed_Cr_Lf_Found_Raw());
            seen_a_lone_lf = true;
        }

        if (c == CR) {
            if (seen_a_lone_lf)
                return FAIL(Error_Mixed_Cr_Lf_Found_Raw());

            dest = Write_Codepoint(dest, LF);
            src = Utf8_Next(&c, src);
            ++n;  // will see '\0' terminator before loop check, so is safe
            if (c == LF) {
                --len_head;  // don't write carraige return, note loss of char
                seen_a_cr_lf = true;
                continue;
            }
            return FAIL(  // DELINE requires any CR to be followed by an LF
                Error_Illegal_Cr(Step_Back_Codepoint(src), String_Head(s))
            );
        }
        dest = Write_Codepoint(dest, c);
    }

    Term_String_Len_Size(s, len_head, dest - Cell_String_At(input));

    return input;
}


//
//  /enline: native [
//
//  "Converts string terminators to native OS format, e.g. LF to CRLF"
//
//      return: [any-string?]
//      string [any-string?] "(modified)"
//  ]
//
DECLARE_NATIVE(enline)
{
    INCLUDE_PARAMS_OF_ENLINE;

    Value* val = ARG(string);

    String* s = Cell_String_Ensure_Mutable(val);
    REBLEN idx = VAL_INDEX(val);

    Length len;
    Size size = Cell_String_Size_Limit_At(&len, val, UNLIMITED);

    REBLEN delta = 0;

    // Calculate the size difference by counting the number of LF's
    // that have no CR's in front of them.
    //
    // !!! The Utf8(*) interface isn't technically necessary if one is
    // counting to the end (one could just go by bytes instead of characters)
    // but this would not work if someone added, say, an ENLINE:PART...since
    // the byte ending position of interest might not be end of the string.

    Utf8(*) cp = String_At(s, idx);

    bool relax = false;  // !!! in case we wanted to tolerate CR LF already?
    Codepoint c_prev = '\0';

    REBLEN n;
    for (n = 0; n < len; ++n) {
        Codepoint c;
        cp = Utf8_Next(&c, cp);
        if (c == LF and (not relax or c_prev != CR))
            ++delta;
        if (c == CR and not relax)  // !!! Note: `relax` fixed at false, ATM
            return FAIL(
                Error_Illegal_Cr(Step_Back_Codepoint(cp), String_Head(s))
            );
        c_prev = c;
    }

    if (delta == 0)
        return COPY(ARG(string)); // nothing to do

    REBLEN old_len = s->misc.length;
    Expand_Flex_Tail(s, delta);  // corrupts str->misc.length
    s->misc.length = old_len + delta;  // just adding CR's

    // One feature of using UTF-8 for strings is that CR/LF substitution can
    // stay a byte-oriented process..because UTF-8 doesn't reuse bytes in the
    // ASCII range, and CR and LF are ASCII.  So as long as the "sliding" is
    // done in terms of byte sizes and not character lengths, it should work.

    Free_Bookmarks_Maybe_Null(s);  // !!! Could this be avoided sometimes?

    Byte* bp = String_Head(s); // expand may change the pointer
    Size tail = String_Size(s); // size in bytes after expansion

    // Add missing CRs

    while (delta > 0) {

        bp[tail--] = bp[size];  // Copy src to dst.

        if (
            bp[size] == LF
            and (
                not relax  // !!! Note: `relax` fixed at false, ATM
                or size == 0
                or bp[size - 1] != CR
            )
        ){
            bp[tail--] = CR;
            --delta;
        }
        --size;
    }

    return COPY(ARG(string));
}


//
//  /entab: native [
//
//  "Converts spaces to tabs (default tab size is 4)"
//
//      return: [any-string?]
//      string "(modified)"
//          [any-string?]
//      :size "Specifies the number of spaces per tab"
//          [integer!]
//  ]
//
DECLARE_NATIVE(entab)
{
    INCLUDE_PARAMS_OF_ENTAB;

    REBINT tabsize;
    if (REF(size))
        tabsize = Int32s(ARG(size), 1);
    else
        tabsize = TAB_SIZE;

    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    REBLEN len = Cell_Series_Len_At(ARG(string));

    Utf8(const*) up = Cell_String_At(ARG(string));
    REBLEN index = VAL_INDEX(ARG(string));

    REBINT n = 0;
    for (; index < len; index++) {
        Codepoint c;
        up = Utf8_Next(&c, up);

        // Count leading spaces, insert TAB for each tabsize:
        if (c == ' ') {
            if (++n >= tabsize) {
                Append_Codepoint(mo->string, '\t');
                n = 0;
            }
            continue;
        }

        // Hitting a leading TAB resets space counter:
        if (c == '\t') {
            Append_Codepoint(mo->string, '\t');
            n = 0;
        }
        else {
            // Incomplete tab space, pad with spaces:
            for (; n > 0; n--)
                Append_Codepoint(mo->string, ' ');

            // Copy chars thru end-of-line (or end of buffer):
            for (; index < len; ++index) {
                if (c == '\n') {
                    //
                    // !!! The original code didn't seem to actually move the
                    // append pointer, it just changed the last character to
                    // a newline.  Was this the intent?
                    //
                    Append_Codepoint(mo->string, '\n');
                    break;
                }
                Append_Codepoint(mo->string, c);
                up = Utf8_Next(&c, up);
            }
        }
    }

    Heart heart = Cell_Heart_Ensure_Noquote(ARG(string));
    return Init_Any_String(OUT, heart, Pop_Molded_String(mo));
}


//
//  /detab: native [
//
//  "Converts tabs to spaces (default tab size is 4)"
//
//      return: [any-string?]
//      string "(modified)"
//          [any-string?]
//      :size "Specifies the number of spaces per tab"
//          [integer!]
//  ]
//
DECLARE_NATIVE(detab)
{
    INCLUDE_PARAMS_OF_DETAB;

    REBLEN len = Cell_Series_Len_At(ARG(string));

    REBINT tabsize;
    if (REF(size))
        tabsize = Int32s(ARG(size), 1);
    else
        tabsize = TAB_SIZE;

    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    // Estimate new length based on tab expansion:

    Utf8(const*) cp = Cell_String_At(ARG(string));
    REBLEN index = VAL_INDEX(ARG(string));

    REBLEN n = 0;

    for (; index < len; ++index) {
        Codepoint c;
        cp = Utf8_Next(&c, cp);

        if (c == '\t') {
            Append_Codepoint(mo->string, ' ');
            n++;
            for (; n % tabsize != 0; n++)
                Append_Codepoint(mo->string, ' ');
            continue;
        }

        if (c == '\n')
            n = 0;
        else
            ++n;

        Append_Codepoint(mo->string, c);
    }

    Heart heart = Cell_Heart_Ensure_Noquote(ARG(string));
    return Init_Any_String(OUT, heart, Pop_Molded_String(mo));
}


//
//  /lowercase: native [
//
//  "Converts string of characters to lowercase"
//
//      return: [any-string? char?]
//      string "(modified if series)"
//          [any-string? char?]
//      :part "Limits to a given length or position"
//          [any-number? any-string?]
//  ]
//
DECLARE_NATIVE(lowercase)
{
    INCLUDE_PARAMS_OF_LOWERCASE;

    Change_Case(OUT, ARG(string), ARG(part), false);
    return OUT;
}


//
//  /uppercase: native [
//
//  "Converts string of characters to uppercase"
//
//      return: [any-string? char?]
//      string "(modified if series)"
//          [any-string? char?]
//      :part "Limits to a given length or position"
//          [any-number? any-string?]
//  ]
//
DECLARE_NATIVE(uppercase)
{
    INCLUDE_PARAMS_OF_UPPERCASE;

    Change_Case(OUT, ARG(string), ARG(part), true);
    return OUT;
}


//
//  /to-hex: native [
//
//  "Converts numeric value to a hex issue! datatype (with leading # and 0's)"
//
//      return: [issue!]
//      value [integer! tuple!]
//      :size "Specify number of hex digits in result"
//          [integer!]
//  ]
//
DECLARE_NATIVE(to_hex)
{
    INCLUDE_PARAMS_OF_TO_HEX;

    Value* arg = ARG(value);

    REBLEN len;
    if (REF(size))
        len = VAL_INT64(ARG(size));
    else
        len = 0;  // !!! avoid compiler warning--but rethink this routine

    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    if (Is_Integer(arg)) {
        if (not REF(size) or len > MAX_HEX_LEN)
            len = MAX_HEX_LEN;

        Form_Hex_Pad(mo, VAL_INT64(arg), len);
    }
    else if (Is_Tuple(arg)) {
        REBLEN n;
        if (
            not REF(size)
            or len > 2 * MAX_TUPLE
            or len > 2 * Cell_Sequence_Len(arg)
        ){
            len = 2 * Cell_Sequence_Len(arg);
        }
        for (n = 0; n != Cell_Sequence_Len(arg); n++)
            Form_Hex2(mo, Cell_Sequence_Byte_At(arg, n));
        for (; n < 3; n++)
            Form_Hex2(mo, 0);
    }
    else
        return FAIL(PARAM(value));

    // !!! Issue should be able to use string from mold buffer directly when
    // UTF-8 Everywhere unification of ANY-WORD? and ANY-STRING? is done.
    //
    assert(len == String_Size(mo->string) - mo->base.size);
    if (not Try_Scan_Issue_To_Stack(Binary_At(mo->string, mo->base.size), len))
        return FAIL(PARAM(value));

    Move_Drop_Top_Stack_Element(OUT);
    Drop_Mold(mo);
    return OUT;
}


//
//  /invalid-utf8?: native [
//
//  "Checks UTF-8 encoding"
//
//      return: "NULL if correct, otherwise position in binary of the error"
//          [~null~ blob!]
//      data [blob!]
//  ]
//
DECLARE_NATIVE(invalid_utf8_q)
//
// !!! A motivation for adding this native was because R3-Alpha did not fully
// validate UTF-8 input, for perceived reasons of performance:
//
// https://github.com/rebol/rebol-issues/issues/638
//
// Ren-C reinstated full validation, as it only causes a hit when a non-ASCII
// sequence is read (which is relatively rare in Rebol).  However, it is
// helpful to have a function that will locate invalid byte sequences if one
// is going to try doing something like substituting a character at the
// invalid positions.
{
    INCLUDE_PARAMS_OF_INVALID_UTF8_Q;

    Value* arg = ARG(data);

    Size size;
    const Byte* utf8 = Cell_Blob_Size_At(&size, arg);

    const Byte* end = utf8 + size;

    REBLEN trail;
    for (; utf8 != end; utf8 += trail) {
        trail = g_trailing_bytes_for_utf8[*utf8] + 1;
        if (utf8 + trail > end or not Is_Legal_UTF8(utf8, trail)) {
            Copy_Cell(OUT, arg);
            VAL_INDEX_RAW(OUT) = utf8 - Binary_Head(Cell_Binary(arg));
            return OUT;
        }
    }

    return nullptr;  // no invalid byte found
}
