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

#include "datatypes/sys-money.h"


//
//  delimit: native [
//
//  {Joins a block of values into TEXT! with delimiters}
//
//      return: "Null if blank input or block's contents are all null"
//          [<opt> text!]
//      delimiter [<opt> blank! char! text!]
//      line "Will be copied if already a text value"
//          [<maybe> text! block! the-block! issue!]
//      /head "Include delimiter at head of result (if non-NULL)"
//      /tail "Include delimiter at tail of result (if non-NULL)"
//  ]
//
DECLARE_NATIVE(delimit)
//
// Evaluates each item in a block and forms it, with an optional delimiter.
// If all the items in the block are null, or no items are found, this will
// return a nulled value.
//
// 1. Erroring on NULL has been found to catch real bugs in practice.  It also
//    enables clever constructs like CURTAIL.
//
// 2. BLOCK!s are prohibitied in DELIMIT because it's too often the case the
//    result is gibberish--guessing what to do is bad:
//
//        >> block: [1 2 <x> hello]
//
//        >> print ["Your block is:" block]
//        Your block is: 12<x>hello  ; ugh.
//
// 3. Because blocks aren't allowed, the usual rule of thumb that blanks should
//    act like empty blocks need not apply.  Instead, the concept is that
//    they act like spaces.  This concept is a long-running experiment that
//    may not pan out, but is cool enough to keep weighing the pros/cons.
//
// 4. CHAR! suppresses the delimiter logic.  Hence:
//
//        >> delimit ":" ["a" space "b" newline void "c" newline "d" "e"]
//        == "a b^/c^/d:e"
//
//    Only the last interstitial is considered a candidate for delimiting.
//
// 5. Empty strings are distinct from voids in terms of still being delimited.
//    This is important, e.g. in comma-delimited formats for empty fields.
//
//    >> delimit "," [field1 field2 field3]  ; field2 is ""
//    one,,three
//
//    The same principle would apply to a "space-delimited format".
{
    INCLUDE_PARAMS_OF_DELIMIT;

    REBVAL *delimiter = ARG(delimiter);
    REBVAL *line = ARG(line);

    if (IS_TEXT(line) or IS_ISSUE(line)) {  // can shortcut, no evals needed
        //
        // Note: It's hard to unify this mold with code below that uses a level
        // due to the asserts on states balancing.  Easiest to repeat a small
        // bit of code!
        //
        DECLARE_MOLD (mo);
        Push_Mold(mo);

        if (REF(head) and REF(delimiter))
            Form_Value(mo, delimiter);

        // Note: This path used to shortcut with running TO TEXT! if not using
        // /HEAD or /TAIL options, but it's probably break-even to invoke the
        // evaluator.  Review optimizations later.
        //
        Form_Value(mo, line);

        if (REF(tail) and REF(delimiter))
            Form_Value(mo, delimiter);

        return Init_Text(OUT, Pop_Molded_String(mo));
    }

    Flags flags = LEVEL_MASK_NONE;
    if (IS_THE_BLOCK(ARG(line)))
        flags |= EVAL_EXECUTOR_FLAG_NO_EVALUATIONS;
    else
        assert(IS_BLOCK(line));

    Level(*) L = Make_Level_At(line, flags);
    Push_Level(OUT, L);

    DECLARE_MOLD (mo);
    Push_Mold(mo);

    // If /HEAD is used, speculatively start the mold out with the delimiter
    // (will be thrown away if the block turns out to make nothing)
    //
    if (REF(head))
        Form_Value(mo, delimiter);

    bool pending = false;  // pending delimiter output, *if* more non-nulls
    bool nothing = true;  // any elements seen so far have been null or blank

    for (; Not_Level_At_End(L); Restart_Evaluator_Level(L)) {
        if (Eval_Step_Throws(OUT, L)) {
            Drop_Mold(mo);
            Drop_Level(L);
            return THROWN;
        }

        if (Is_Elision(OUT))  // spaced [elide print "hi"], etc
            continue;  // vaporize

        Decay_If_Unstable(OUT);  // spaced [match [logic!] false ...]

        if (Is_Void(OUT))  // spaced [maybe null], spaced [if false [<a>]], etc
            continue;  // vaporize

        if (Is_Nulled(OUT))  // catches bugs in practice, see [1]
            return RAISE(Error_Need_Non_Null_Raw());

        if (Is_Isotope(OUT))
            return RAISE(Error_Bad_Isotope(OUT));

        if (ANY_ARRAY(OUT))  // guessing a behavior is bad, see [2]
            fail ("Desired array rendering in DELIMIT not known");

        nothing = false;

        if (IS_BLANK(OUT)) {  // BLANK! acts as space, see [3]
            Append_Codepoint(mo->series, ' ');
            pending = false;
        }
        else if (IS_ISSUE(OUT)) {  // do not delimit (unified w/char), see [4]
            Form_Value(mo, OUT);
            pending = false;
        }
        else {
            if (pending and REF(delimiter))
                Form_Value(mo, delimiter);

            if (Is_Quoted(OUT)) {
                Unquotify(OUT, 1);
                Mold_Value(mo, OUT);
            }
            else
                Form_Value(mo, OUT);

            pending = true;  // note this includes empty strings, see [5]
        }
    } while (Not_Level_At_End(L));

    if (nothing) {
        Drop_Mold(mo);
        Init_Nulled(OUT);
    }
    else {
        if (REF(tail) and REF(delimiter))
            Form_Value(mo, delimiter);
        Init_Text(OUT, Pop_Molded_String(mo));
    }

    Drop_Level(L);

    return OUT;
}


//
//  debase: native [
//
//  {Decodes binary-coded string (BASE-64 default) to binary value.}
//
//      return: [binary!]
//          ; Comment said "we don't know the encoding" of the return binary
//      value [binary! text!]
//      /base "The base to convert from: 64, 16, or 2 (defaults to 64)"
//          [integer!]
//  ]
//
DECLARE_NATIVE(debase)
{
    INCLUDE_PARAMS_OF_DEBASE;

    Size size;
    const Byte* bp = VAL_BYTES_AT(&size, ARG(value));

    REBINT base = 64;
    if (REF(base))
        base = VAL_INT32(ARG(base));
    else
        base = 64;

    if (!Decode_Binary(OUT, bp, size, base, 0))
        fail (Error_Invalid_Data_Raw(ARG(value)));

    return OUT;
}


//
//  enbase: native [
//
//  {Encodes data into a binary, hexadecimal, or base-64 ASCII string.}
//
//      return: [text!]
//      value "If text, will be UTF-8 encoded"
//          [binary! text!]
//      /base "Binary base to use: 64, 16, or 2 (BASE-64 default)"
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
    const Byte* bp = VAL_BYTES_AT(&size, ARG(value));

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
//  enhex: native [
//
//  "Converts string to use URL-style hex encoding (%XX)"
//
//      return: [any-string!]
//          "See http://en.wikipedia.org/wiki/Percent-encoding"
//      string [any-string!]
//          "String to encode, all non-ASCII or illegal URL bytes encoded"
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
    Utf8(const*) cp = VAL_UTF8_LEN_SIZE_AT(&len, nullptr, ARG(string));

    Codepoint c;
    cp = NEXT_CHR(&c, cp);

    REBLEN i;
    for (i = 0; i < len; cp = NEXT_CHR(&c, cp), ++i) {
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
            // "Everything else must be url-encoded".  Rebol's LEX_MAP does
            // not have a bit for this in particular, though maybe it could
            // be retooled to help more with this.  For now just use it to
            // speed things up a little.

            encoded[0] = cast(Byte, c);
            encoded_size = 1;

            switch (GET_LEX_CLASS(c)) {
              case LEX_CLASS_DELIMIT:
                switch (GET_LEX_VALUE(c)) {
                  case LEX_DELIMIT_LEFT_PAREN:
                  case LEX_DELIMIT_RIGHT_PAREN:
                  case LEX_DELIMIT_LEFT_BRACKET:
                  case LEX_DELIMIT_RIGHT_BRACKET:
                  case LEX_DELIMIT_SLASH:
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

                  case LEX_DELIMIT_UTF8_ERROR:  // not for c < 0x80
                  default:
                    panic ("Internal LEX_DELIMIT table error");
                }
                goto leave_as_is;

              case LEX_CLASS_SPECIAL:
                switch (GET_LEX_VALUE(c)) {
                  case LEX_SPECIAL_AT:
                  case LEX_SPECIAL_COLON:
                  case LEX_SPECIAL_APOSTROPHE:
                  case LEX_SPECIAL_PLUS:
                  case LEX_SPECIAL_MINUS:
                  case LEX_SPECIAL_BLANK:
                  case LEX_SPECIAL_POUND:
                  case LEX_SPECIAL_DOLLAR:
                  case LEX_SPECIAL_SEMICOLON:
                    goto leave_as_is;

                  case LEX_SPECIAL_WORD:
                    assert(false);  // only occurs in use w/Prescan_Token()
                    goto leave_as_is;

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
            Append_Codepoint(mo->series, c);
            continue;
        }

    needs_encoding:;
      #if !defined(NDEBUG)
        if (c < 0x80)
           assert(strchr(no_encode, c) == NULL);
      #endif

        REBLEN n;
        for (n = 0; n != encoded_size; ++n) {
            Append_Codepoint(mo->series, '%');

            // Use uppercase hex digits, per RFC 3896 2.1, which is also
            // consistent with JavaScript's encodeURIComponent()
            //
            // https://tools.ietf.org/html/rfc3986#section-2.1
            //
            Append_Codepoint(mo->series, Hex_Digits[(encoded[n] & 0xf0) >> 4]);
            Append_Codepoint(mo->series, Hex_Digits[encoded[n] & 0xf]);
        }
    }

    Init_Any_String(OUT, VAL_TYPE(ARG(string)), Pop_Molded_String(mo));
    return OUT;
}


//
//  dehex: native [
//
//  "Converts URL-style encoded strings, %XX is interpreted as UTF-8 byte."
//
//      return: [any-string!]
//          "Decoded string, with the same string type as the input."
//      string [any-string!]
//          "See http://en.wikipedia.org/wiki/Percent-encoding"
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
    Utf8(const*) cp = VAL_UTF8_LEN_SIZE_AT(&len, nullptr, ARG(string));

    Codepoint c;
    cp = NEXT_CHR(&c, cp);

    REBLEN i;
    for (i = 0; i < len;) {
        if (c != '%')
            Append_Codepoint(mo->series, c);
        else {
            if (i + 2 >= len)
               fail ("Percent decode has less than two codepoints after %");

            cp = NEXT_CHR(&c, cp);
            ++i;
            if (c > UINT8_MAX)
                c = '\0'; // LEX_DELIMIT, will cause error below
            Byte lex1 = Lex_Map[cast(Byte, c)];

            cp = NEXT_CHR(&c, cp);
            ++i;
            if (c > UINT8_MAX)
                c = '\0'; // LEX_DELIMIT, will cause error below
            Byte lex2 = Lex_Map[cast(Byte, c)];

            // If class LEX_WORD or LEX_NUMBER, there is a value contained in
            // the mask which is the value of that "digit".  So A-F and
            // a-f can quickly get their numeric values.
            //
            Byte d1 = lex1 & LEX_VALUE;
            Byte d2 = lex2 & LEX_VALUE;

            if (
                lex1 < LEX_WORD or (d1 == 0 and lex1 < LEX_NUMBER)
                or lex2 < LEX_WORD or (d2 == 0 and lex2 < LEX_NUMBER)
            ){
                fail ("Percent must be followed by 2 hex digits, e.g. %XX");
            }

            // !!! We might optimize here for ASCII codepoints, but would
            // need to consider it a "flushing point" for the scan buffer,
            // in order to not gloss over incomplete UTF-8 sequences.
            //
            Byte b = (d1 << 4) + d2;
            scan[scan_size++] = b;
        }

        cp = NEXT_CHR(&c, cp); // c may be '\0', guaranteed if `i == len`
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

            Append_Codepoint(mo->series, decoded);
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

    Init_Any_String(OUT, VAL_TYPE(ARG(string)), Pop_Molded_String(mo));
    return OUT;
}


//
//  deline: native [
//
//  {Converts string terminators to standard format, e.g. CR LF to LF.}
//
//      return: [text! block!]
//      input "Will be modified (unless /LINES used)"
//          [text! binary!]
//      /lines "Return block of lines (works for LF, CR-LF endings)"
//  ]
//
DECLARE_NATIVE(deline)
{
    INCLUDE_PARAMS_OF_DELINE;

    // AS TEXT! verifies the UTF-8 validity of a BINARY!, and checks for any
    // embedded '\0' bytes, illegal in texts...without copying the input.
    //
    REBVAL *input = rebValue("as text!", ARG(input));

    if (REF(lines)) {
        Init_Block(OUT, Split_Lines(input));
        rebRelease(input);
        return OUT;
    }

    String(*) s = VAL_STRING_Ensure_Mutable(input);
    REBLEN len_head = String_Len(s);

    REBLEN len_at = VAL_LEN_AT(input);

    Utf8(*) dest = VAL_STRING_AT_Known_Mutable(input);
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
        src = NEXT_CHR(&c, src);
        ++n;
        if (c == LF) {
            if (seen_a_cr_lf)
                fail (Error_Mixed_Cr_Lf_Found_Raw());
            seen_a_lone_lf = true;
        }

        if (c == CR) {
            if (seen_a_lone_lf)
                fail (Error_Mixed_Cr_Lf_Found_Raw());

            dest = WRITE_CHR(dest, LF);
            src = NEXT_CHR(&c, src);
            ++n;  // will see '\0' terminator before loop check, so is safe
            if (c == LF) {
                --len_head;  // don't write carraige return, note loss of char
                seen_a_cr_lf = true;
                continue;
            }
            // DELINE requires any CR to be followed by an LF
            fail (Error_Illegal_Cr(BACK_STR(src), String_Head(s)));
        }
        dest = WRITE_CHR(dest, c);
    }

    Term_String_Len_Size(s, len_head, dest - VAL_STRING_AT(input));

    return input;
}


//
//  enline: native [
//
//  {Converts string terminators to native OS format, e.g. LF to CRLF.}
//
//      return: [any-string!]
//      string [any-string!] "(modified)"
//  ]
//
DECLARE_NATIVE(enline)
{
    INCLUDE_PARAMS_OF_ENLINE;

    REBVAL *val = ARG(string);

    String(*) s = VAL_STRING_Ensure_Mutable(val);
    REBLEN idx = VAL_INDEX(val);

    Length len;
    Size size = VAL_SIZE_LIMIT_AT(&len, val, UNLIMITED);

    REBLEN delta = 0;

    // Calculate the size difference by counting the number of LF's
    // that have no CR's in front of them.
    //
    // !!! The Utf8(*) interface isn't technically necessary if one is
    // counting to the end (one could just go by bytes instead of characters)
    // but this would not work if someone added, say, an ENLINE/PART...since
    // the byte ending position of interest might not be end of the string.

    Utf8(*) cp = String_At(s, idx);

    bool relax = false;  // !!! in case we wanted to tolerate CR LF already?
    Codepoint c_prev = '\0';

    REBLEN n;
    for (n = 0; n < len; ++n) {
        Codepoint c;
        cp = NEXT_CHR(&c, cp);
        if (c == LF and (not relax or c_prev != CR))
            ++delta;
        if (c == CR and not relax)  // !!! Note: `relax` fixed at false, ATM
            fail (Error_Illegal_Cr(BACK_STR(cp), String_Head(s)));
        c_prev = c;
    }

    if (delta == 0)
        return COPY(ARG(string)); // nothing to do

    REBLEN old_len = s->misc.length;
    Expand_Series_Tail(s, delta);  // corrupts str->misc.length
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
//  entab: native [
//
//  "Converts spaces to tabs (default tab size is 4)."
//
//      return: [any-string!]
//      string "(modified)"
//          [any-string!]
//      /size "Specifies the number of spaces per tab"
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

    REBLEN len = VAL_LEN_AT(ARG(string));

    Utf8(const*) up = VAL_STRING_AT(ARG(string));
    REBLEN index = VAL_INDEX(ARG(string));

    REBINT n = 0;
    for (; index < len; index++) {
        Codepoint c;
        up = NEXT_CHR(&c, up);

        // Count leading spaces, insert TAB for each tabsize:
        if (c == ' ') {
            if (++n >= tabsize) {
                Append_Codepoint(mo->series, '\t');
                n = 0;
            }
            continue;
        }

        // Hitting a leading TAB resets space counter:
        if (c == '\t') {
            Append_Codepoint(mo->series, '\t');
            n = 0;
        }
        else {
            // Incomplete tab space, pad with spaces:
            for (; n > 0; n--)
                Append_Codepoint(mo->series, ' ');

            // Copy chars thru end-of-line (or end of buffer):
            for (; index < len; ++index) {
                if (c == '\n') {
                    //
                    // !!! The original code didn't seem to actually move the
                    // append pointer, it just changed the last character to
                    // a newline.  Was this the intent?
                    //
                    Append_Codepoint(mo->series, '\n');
                    break;
                }
                Append_Codepoint(mo->series, c);
                up = NEXT_CHR(&c, up);
            }
        }
    }

    enum Reb_Kind kind = VAL_TYPE(ARG(string));
    return Init_Any_String(OUT, kind, Pop_Molded_String(mo));
}


//
//  detab: native [
//
//  "Converts tabs to spaces (default tab size is 4)."
//
//      return: [any-string!]
//      string "(modified)"
//          [any-string!]
//      /size "Specifies the number of spaces per tab"
//          [integer!]
//  ]
//
DECLARE_NATIVE(detab)
{
    INCLUDE_PARAMS_OF_DETAB;

    REBLEN len = VAL_LEN_AT(ARG(string));

    REBINT tabsize;
    if (REF(size))
        tabsize = Int32s(ARG(size), 1);
    else
        tabsize = TAB_SIZE;

    DECLARE_MOLD (mo);
    Push_Mold(mo);

    // Estimate new length based on tab expansion:

    Utf8(const*) cp = VAL_STRING_AT(ARG(string));
    REBLEN index = VAL_INDEX(ARG(string));

    REBLEN n = 0;

    for (; index < len; ++index) {
        Codepoint c;
        cp = NEXT_CHR(&c, cp);

        if (c == '\t') {
            Append_Codepoint(mo->series, ' ');
            n++;
            for (; n % tabsize != 0; n++)
                Append_Codepoint(mo->series, ' ');
            continue;
        }

        if (c == '\n')
            n = 0;
        else
            ++n;

        Append_Codepoint(mo->series, c);
    }

    enum Reb_Kind kind = VAL_TYPE(ARG(string));
    return Init_Any_String(OUT, kind, Pop_Molded_String(mo));
}


//
//  lowercase: native [
//
//  "Converts string of characters to lowercase."
//
//      return: [any-string! char!]
//      string "(modified if series)"
//          [any-string! char!]
//      /part "Limits to a given length or position"
//          [any-number! any-string!]
//  ]
//
DECLARE_NATIVE(lowercase)
{
    INCLUDE_PARAMS_OF_LOWERCASE;

    Change_Case(OUT, ARG(string), ARG(part), false);
    return OUT;
}


//
//  uppercase: native [
//
//  "Converts string of characters to uppercase."
//
//      return: [any-string! char!]
//      string "(modified if series)"
//          [any-string! char!]
//      /part "Limits to a given length or position"
//          [any-number! any-string!]
//  ]
//
DECLARE_NATIVE(uppercase)
{
    INCLUDE_PARAMS_OF_UPPERCASE;

    Change_Case(OUT, ARG(string), ARG(part), true);
    return OUT;
}


//
//  to-hex: native [
//
//  {Converts numeric value to a hex issue! datatype (with leading # and 0's).}
//
//      return: [issue!]
//      value [integer! tuple!]
//      /size "Specify number of hex digits in result"
//          [integer!]
//  ]
//
DECLARE_NATIVE(to_hex)
{
    INCLUDE_PARAMS_OF_TO_HEX;

    REBVAL *arg = ARG(value);

    REBLEN len;
    if (REF(size))
        len = cast(REBLEN, VAL_INT64(ARG(size)));
    else
        len = cast(REBLEN, UNLIMITED);

    DECLARE_MOLD (mo);
    Push_Mold(mo);

    if (IS_INTEGER(arg)) {
        if (len == cast(REBLEN, UNLIMITED) || len > MAX_HEX_LEN)
            len = MAX_HEX_LEN;

        Form_Hex_Pad(mo, VAL_INT64(arg), len);
    }
    else if (IS_TUPLE(arg)) {
        REBLEN n;
        if (
            len == cast(REBLEN, UNLIMITED)
            || len > 2 * MAX_TUPLE
            || len > cast(REBLEN, 2 * VAL_SEQUENCE_LEN(arg))
        ){
            len = 2 * VAL_SEQUENCE_LEN(arg);
        }
        for (n = 0; n != VAL_SEQUENCE_LEN(arg); n++)
            Form_Hex2(mo, VAL_SEQUENCE_BYTE_AT(arg, n));
        for (; n < 3; n++)
            Form_Hex2(mo, 0);
    }
    else
        fail (PARAM(value));

    // !!! Issue should be able to use string from mold buffer directly when
    // UTF-8 Everywhere unification of ANY-WORD! and ANY-STRING! is done.
    //
    assert(len == String_Size(mo->series) - mo->base.size);
    if (NULL == Scan_Issue(OUT, Binary_At(mo->series, mo->base.size), len))
        fail (PARAM(value));

    Drop_Mold(mo);
    return OUT;
}


//
//  invalid-utf8?: native [
//
//  {Checks UTF-8 encoding}
//
//      return: "NULL if correct, otherwise position in binary of the error"
//          [<opt> binary!]
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

    REBVAL *arg = ARG(data);

    Size size;
    const Byte* utf8 = VAL_BINARY_SIZE_AT(&size, arg);

    const Byte* end = utf8 + size;

    REBLEN trail;
    for (; utf8 != end; utf8 += trail) {
        trail = trailingBytesForUTF8[*utf8] + 1;
        if (utf8 + trail > end or not isLegalUTF8(utf8, trail)) {
            Copy_Cell(OUT, arg);
            VAL_INDEX_RAW(OUT) = utf8 - Binary_Head(VAL_BINARY(arg));
            return OUT;
        }
    }

    return nullptr;  // no invalid byte found
}
