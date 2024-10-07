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
// If all the items in the block are null, or no items are found, this will
// return a nulled value.
//
// 1. It's hard to unify this mold with code below that uses a level due to
//    the asserts on states balancing.  Easiest to repeat a small bit of code!
{
    INCLUDE_PARAMS_OF_DELIMIT;

    Element* line = cast(Element*, ARG(line));

    Option(const Element*) delimiter;
    if (REF(delimiter))
        delimiter = cast(const Element*, ARG(delimiter));
    else
        delimiter = nullptr;

    if (Is_Block(line) or Is_The_Block(line))
        goto delimit_block;

  { ///////////////////////////////////////////////////////////////////////////

    assert(Is_Text(line) or Is_Issue(line));  // shortcut, no evals needed [1]

    DECLARE_MOLD (mo);
    Push_Mold(mo);

    if (REF(head) and delimiter)
        Form_Element(mo, unwrap delimiter);

    Form_Element(mo, cast(Element*, line));

    if (REF(tail) and delimiter)
        Form_Element(mo, unwrap delimiter);

    return Init_Text(OUT, Pop_Molded_String(mo));

} delimit_block: { ////////////////////////////////////////////////////////////

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
    //
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

    Flags flags = LEVEL_MASK_NONE;
    if (Is_The_Block(ARG(line)))
        flags |= EVAL_EXECUTOR_FLAG_NO_EVALUATIONS;
    else
        assert(Is_Block(line));

    Level* L = Make_Level_At(&Stepper_Executor, line, flags);
    Push_Level(OUT, L);

    DECLARE_MOLD (mo);
    Push_Mold(mo);

    bool pending = false;  // pending delimiter output, *if* more non-nulls
    bool nothing = true;  // all elements seen so far have been void

    if (REF(head) and delimiter)  // speculatively start with delimiter
        Form_Element(mo, unwrap delimiter);  // (thrown out if `nothing` made)

    while (Not_Level_At_End(L)) {
        const Element* item = At_Level(L);
        if (Is_Block(item) and REF(delimiter)) {  // hack [1]
            Derelativize(SPARE, item, Level_Binding(L));
            Fetch_Next_In_Feed(L->feed);

            Value* unspaced = rebValue(Canon(UNSPACED), rebQ(SPARE));
            if (unspaced == nullptr)  // vaporized, allow it
                continue;

            Copy_Cell(OUT, unspaced);
            rebRelease(unspaced);
        }
        else if (Is_Blank(item)) {  // BLANK! acts as space [2]
            Append_Codepoint(mo->string, ' ');
            pending = false;
            nothing = false;
            Fetch_Next_In_Feed(L->feed);
            continue;
        }
        else if (Any_The_Value(item)) {  // fetch and mold
            if (Is_The_Word(item) or Is_The_Tuple(item)) {
                Get_Var_May_Fail(OUT, item, Level_Binding(L));
            }
            else if (Is_The_Group(item)) {
                if (Eval_Any_List_At_Throws(
                    OUT,
                    item,
                    Level_Binding(L)
                )){
                    goto threw;
                }
            }
            else
                fail (item);

            Fetch_Next_In_Feed(L->feed);

            Decay_If_Unstable(OUT);
            Value* molded = rebValue(Canon(MOLD), rebQ(stable_OUT));
            if (molded == nullptr)  // vaporized (e.g. MOLD of VOID)
                continue;
            Copy_Cell(OUT, molded);
            rebRelease(molded);
        }
        else {
            if (Eval_Step_Throws(OUT, L))
                goto threw;

            Restart_Stepper_Level(L);
        }

        if (Is_Elision(OUT))  // spaced [elide print "hi"], etc
            continue;  // vaporize

        Decay_If_Unstable(OUT);  // spaced [match [logic?] false ...]

        if (Is_Void(OUT))  // spaced [maybe null], spaced [if null [<a>]], etc
            continue;  // vaporize

        if (Is_Nulled(OUT))  // catches bugs in practice [3]
            return RAISE(Error_Need_Non_Null_Raw());

        if (Is_Antiform(OUT))
            return RAISE(Error_Bad_Antiform(OUT));

        if (Any_List(OUT))  // guessing a behavior is bad [4]
            fail ("Desired list rendering in DELIMIT not known");

        if (Sigil_Of(cast(Element*, OUT)))
            fail ("DELIMIT requires @var to render elements with sigils");

        if (Is_Blank(OUT))
            fail ("DELIMIT only treats source-level BLANK! as space");

        nothing = false;

        if (Is_Issue(OUT)) {  // do not delimit (unified w/char) [5]
            Form_Element(mo, cast(Element*, OUT));
            pending = false;
        }
        else {
            if (pending and delimiter)
                Form_Element(mo, unwrap delimiter);

            Form_Element(mo, cast(Element*, OUT));

            pending = true;  // note this includes empty strings [6]
        }
    } while (Not_Level_At_End(L));

    if (nothing) {
        Drop_Mold(mo);
        Init_Nulled(OUT);
    }
    else {
        if (REF(tail) and delimiter)
            Form_Element(mo, unwrap delimiter);
        Init_Text(OUT, Pop_Molded_String(mo));
    }

    Drop_Level(L);
    return OUT;

  threw: //////////////////////////////////////////////////////////////////////

    Drop_Mold(mo);
    Drop_Level(L);
    return THROWN;
}}


//
//  /debase: native [
//
//  "Decodes base-coded string (BASE-64 default) to binary value"
//
//      return: [binary!]
//      value [binary! text!]
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
        fail (Error_Invalid_Data_Raw(ARG(value)));

    return Init_Blob(OUT, decoded);
}


//
//  /enbase: native [
//
//  "Encodes data into a binary, hexadecimal, or base-64 ASCII string"
//
//      return: [text!]
//      value "If text, will be UTF-8 encoded"
//          [binary! text!]
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

    DECLARE_MOLD (mo);
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
        fail (PARAM(base));
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
{
    INCLUDE_PARAMS_OF_ENHEX;

    // The details of what ASCII characters must be percent encoded
    // are contained in RFC 3896, but a summary is here:
    //
    // https://stackoverflow.com/a/7109208/
    //
    // Everything but: A-Z a-z 0-9 - . _ ~ : / ? # [ ] @ ! $ & ' ( ) * + , ; =
    //
  #if !defined(NDEBUG)
    const char *no_encode =
        "ABCDEFGHIJKLKMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789" \
            "-._~:/?#[]@!$&'()*+,;=";
  #endif

    DECLARE_MOLD (mo);
    Push_Mold (mo);

    REBLEN len;
    Utf8(const*) cp = Cell_Utf8_Len_Size_At(&len, nullptr, ARG(string));

    Codepoint c;
    cp = Utf8_Next(&c, cp);

    REBLEN i;
    for (i = 0; i < len; cp = Utf8_Next(&c, cp), ++i) {
        //
        // !!! Length 4 should be legal here, but a warning in an older GCC
        // is complaining that Encode_UTF8_Char reaches out of array bounds
        // when it does not appear to.  Possibly related to this:
        //
        // https://gcc.gnu.org/bugzilla/show_bug.cgi?id=43949
        //
        Byte encoded[UNI_ENCODED_MAX];
        REBLEN encoded_size;

        if (c >= 0x80) {  // all non-ASCII characters *must* be percent encoded
            encoded_size = Encoded_Size_For_Codepoint(c);
            Encode_UTF8_Char(encoded, c, encoded_size);
        }
        else {
            // "Everything else must be url-encoded".  Rebol's g_lex_map does
            // not have a bit for this in particular, though maybe it could
            // be retooled to help more with this.  For now just use it to
            // speed things up a little.

            encoded[0] = cast(Byte, c);
            encoded_size = 1;

            switch (Get_Lex_Class(encoded[0])) {
              case LEX_CLASS_DELIMIT:
                switch (Get_Lex_Delimit(encoded[0])) {
                  case LEX_DELIMIT_LEFT_PAREN:
                  case LEX_DELIMIT_RIGHT_PAREN:
                  case LEX_DELIMIT_LEFT_BRACKET:
                  case LEX_DELIMIT_RIGHT_BRACKET:
                  case LEX_DELIMIT_SLASH:
                  case LEX_DELIMIT_COLON:
                  case LEX_DELIMIT_PERIOD:
                  case LEX_DELIMIT_COMMA:
                  case LEX_DELIMIT_TILDE:
                    goto leave_as_is;

                  case LEX_DELIMIT_SPACE:  // includes control characters
                  case LEX_DELIMIT_END:  // 00 null terminator
                  case LEX_DELIMIT_LINEFEED:
                  case LEX_DELIMIT_RETURN:  // e.g. ^M
                  case LEX_DELIMIT_LEFT_BRACE:
                  case LEX_DELIMIT_RIGHT_BRACE:
                  case LEX_DELIMIT_DOUBLE_QUOTE:
                    goto needs_encoding;

                  default:
                    panic ("Internal LEX_DELIMIT table error");
                }
                goto leave_as_is;

              case LEX_CLASS_SPECIAL:
                switch (Get_Lex_Special(encoded[0])) {
                  case LEX_SPECIAL_AT:
                  case LEX_SPECIAL_APOSTROPHE:
                  case LEX_SPECIAL_PLUS:
                  case LEX_SPECIAL_MINUS:
                  case LEX_SPECIAL_UNDERSCORE:
                  case LEX_SPECIAL_POUND:
                  case LEX_SPECIAL_DOLLAR:
                  case LEX_SPECIAL_SEMICOLON:
                    goto leave_as_is;

                  case LEX_SPECIAL_WORD:
                    assert(false);  // only occurs in use w/Prescan_Token()
                    goto leave_as_is;

                  case LEX_SPECIAL_UTF8_ERROR:  // not for c < 0x80
                    assert(false);
                    panic (nullptr);

                  default:
                    goto needs_encoding;
                }
                goto leave_as_is;

              case LEX_CLASS_WORD:
                if (
                    (c >= 'a' and c <= 'z') or (c >= 'A' and c <= 'Z')
                    or c == '?' or c == '!' or c == '&'
                    or c == '*' or c == '='
                ){
                    goto leave_as_is;  // this is all that's leftover
                }
                goto needs_encoding;

              case LEX_CLASS_NUMBER:
                goto leave_as_is;  // 0-9 needs no encoding.
            }

        leave_as_is:;
          #if !defined(NDEBUG)
            assert(strchr(no_encode, c) != NULL);
          #endif
            Append_Codepoint(mo->string, c);
            continue;
        }

    needs_encoding:;
      #if !defined(NDEBUG)
        if (c < 0x80)
           assert(strchr(no_encode, c) == NULL);
      #endif

        REBLEN n;
        for (n = 0; n != encoded_size; ++n) {
            Append_Codepoint(mo->string, '%');

            // Use uppercase hex digits, per RFC 3896 2.1, which is also
            // consistent with JavaScript's encodeURIComponent()
            //
            // https://tools.ietf.org/html/rfc3986#section-2.1
            //
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
//  ]
//
DECLARE_NATIVE(dehex)
{
    INCLUDE_PARAMS_OF_DEHEX;

    DECLARE_MOLD (mo);
    Push_Mold(mo);

    // RFC 3986 says the encoding/decoding must use UTF-8.  This temporary
    // buffer is used to hold up to 4 bytes (and a terminator) that need
    // UTF-8 decoding--the maximum one UTF-8 encoded codepoint may have.
    //
    Byte scan[5];
    Size scan_size = 0;

    REBLEN len;
    Utf8(const*) cp = Cell_Utf8_Len_Size_At(&len, nullptr, ARG(string));

    Codepoint c;
    cp = Utf8_Next(&c, cp);

    REBLEN i;
    for (i = 0; i < len;) {
        if (c != '%')
            Append_Codepoint(mo->string, c);
        else {
            if (i + 2 >= len)
               fail ("Percent decode has less than two codepoints after %");

            Codepoint c1;
            Codepoint c2;
            cp = Utf8_Next(&c1, cp);
            ++i;
            cp = Utf8_Next(&c2, cp);
            ++i;

            Byte nibble1;
            Byte nibble2;
            if (
                c1 > UINT8_MAX
                or not Try_Get_Lex_Hexdigit(&nibble1, cast(Byte, c1))
                or
                c2 > UINT8_MAX
                or not Try_Get_Lex_Hexdigit(&nibble2, cast(Byte, c2))
            ){
                fail ("Percent must be followed by 2 hex digits, e.g. %XX");
            }

            Byte b = (nibble1 << 4) + nibble2;
            scan[scan_size++] = b;
        }

        cp = Utf8_Next(&c, cp); // c may be '\0', guaranteed if `i == len`
        ++i;

        // If our scanning buffer is full (and hence should contain at *least*
        // one full codepoint) or there are no more UTF-8 bytes coming (due
        // to end of string or the next input not a %XX pattern), then try
        // to decode what we've got.
        //
        if (scan_size > 0 and (c != '%' or scan_size == 4)) {
            assert(i != len or c == '\0');

          decode_codepoint:
            scan[scan_size] = '\0';
            const Byte* next; // goto would cross initialization
            Codepoint decoded;
            if (scan[0] < 0x80) {
                decoded = scan[0];
                next = &scan[0]; // last byte is only byte (see Back_Scan)
            }
            else {
                next = Back_Scan_UTF8_Char(&decoded, scan, &scan_size);
                if (next == NULL)
                    fail ("Bad UTF-8 sequence in %XX of dehex");
            }

            // !!! Should you be able to give a BINARY! to be dehexed and then
            // get a BINARY! back that permits internal zero chars?  This
            // would not be guaranteeing UTF-8 compatibility.  Seems dodgy.
            //
            if (decoded == '\0')
                fail (Error_Illegal_Zero_Byte_Raw());

            Append_Codepoint(mo->string, decoded);
            --scan_size; // one less (see why it's called "Back_Scan")

            // Slide any residual UTF-8 data to the head of the buffer
            //
            REBLEN n;
            for (n = 0; n < scan_size; ++n) {
                ++next; // pre-increment (see why it's called "Back_Scan")
                scan[n] = *next;
            }

            // If we still have bytes left in the buffer and no more bytes
            // are coming, this is the last chance to decode those bytes,
            // keep going.
            //
            if (scan_size != 0 and c != '%')
                goto decode_codepoint;
        }
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
//          [text! binary!]
//      :lines "Return block of lines (works for LF, CR-LF endings)"
//  ]
//
DECLARE_NATIVE(deline)
{
    INCLUDE_PARAMS_OF_DELINE;

    // AS TEXT! verifies the UTF-8 validity of a BINARY!, and checks for any
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
                fail (Error_Mixed_Cr_Lf_Found_Raw());
            seen_a_lone_lf = true;
        }

        if (c == CR) {
            if (seen_a_lone_lf)
                fail (Error_Mixed_Cr_Lf_Found_Raw());

            dest = Write_Codepoint(dest, LF);
            src = Utf8_Next(&c, src);
            ++n;  // will see '\0' terminator before loop check, so is safe
            if (c == LF) {
                --len_head;  // don't write carraige return, note loss of char
                seen_a_cr_lf = true;
                continue;
            }
            // DELINE requires any CR to be followed by an LF
            fail (Error_Illegal_Cr(Step_Back_Codepoint(src), String_Head(s)));
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
            fail (Error_Illegal_Cr(Step_Back_Codepoint(cp), String_Head(s)));
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

    DECLARE_MOLD (mo);
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

    DECLARE_MOLD (mo);
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

    DECLARE_MOLD (mo);
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
        fail (PARAM(value));

    // !!! Issue should be able to use string from mold buffer directly when
    // UTF-8 Everywhere unification of ANY-WORD? and ANY-STRING? is done.
    //
    assert(len == String_Size(mo->string) - mo->base.size);
    if (not Try_Scan_Issue_To_Stack(Binary_At(mo->string, mo->base.size), len))
        fail (PARAM(value));

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
//          [~null~ binary!]
//      data [binary!]
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
    const Byte* utf8 = Cell_Binary_Size_At(&size, arg);

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
