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
#include "sys-deci-funcs.h"

#include "sys-zlib.h"

/***********************************************************************
**
**  Hash Function Externs
**
***********************************************************************/

#if !defined(SHA_DEFINED) && defined(HAS_SHA1)
    // make-headers.r outputs a prototype already, because it is used by cloak
    // (triggers warning -Wredundant-decls)
    // Byte *SHA1(Byte *, REBLEN, Byte *);

    EXTERN_C void SHA1_Init(void *c);
    EXTERN_C void SHA1_Update(void *c, Byte *data, REBLEN len);
    EXTERN_C void SHA1_Final(Byte *md, void *c);
    EXTERN_C int SHA1_CtxSize(void);
#endif

#if !defined(MD5_DEFINED) && defined(HAS_MD5)
    EXTERN_C void MD5_Init(void *c);
    EXTERN_C void MD5_Update(void *c, Byte *data, REBLEN len);
    EXTERN_C void MD5_Final(Byte *md, void *c);
    EXTERN_C int MD5_CtxSize(void);
#endif

#ifdef HAS_MD4
    Byte *MD4(Byte *, REBLEN, Byte *);

    EXTERN_C void MD4_Init(void *c);
    EXTERN_C void MD4_Update(void *c, Byte *data, REBLEN len);
    EXTERN_ void MD4_Final(Byte *md, void *c);
    EXTERN_C int MD4_CtxSize(void);
#endif


// Table of has functions and parameters:
static struct {
    Byte *(*digest)(Byte *, REBLEN, Byte *);
    void (*init)(void *);
    void (*update)(void *, Byte *, REBLEN);
    void (*final)(Byte *, void *);
    int (*ctxsize)(void);
    SymId sym;
    REBLEN len;
    REBLEN hmacblock;
} digests[] = {

#ifdef HAS_SHA1
    {SHA1, SHA1_Init, SHA1_Update, SHA1_Final, SHA1_CtxSize, SYM_SHA1, 20, 64},
#endif

#ifdef HAS_MD4
    {MD4, MD4_Init, MD4_Update, MD4_Final, MD4_CtxSize, SYM_MD4, 16, 64},
#endif

#ifdef HAS_MD5
    {MD5, MD5_Init, MD5_Update, MD5_Final, MD5_CtxSize, SYM_MD5, 16, 64},
#endif

    {nullptr, nullptr, nullptr, nullptr, nullptr, SYM_0_internal, 0, 0}

};


//
//  delimit: native [
//
//  {Joins a block of values into TEXT! with delimiters}
//
//      return: "Null if blank input or block's contents are all null"
//          [~null~ text!]
//      delimiter [~null~ ~void~ char! text!]
//      line "Will be copied if already a text value"
//          [<maybe> text! block!]
//      /tail "Include delimiter at tail of result (if non-NULL)"
//  ]
//
DECLARE_NATIVE(delimit)
{
    INCLUDE_PARAMS_OF_DELIMIT;

    Value* line = ARG(line);
    if (IS_TEXT(line))
        return rebValue("copy", line);  // !!! Review performance

    assert(IS_BLOCK(line));

    if (Form_Reduce_Throws(
        OUT,
        Cell_Array(line),
        VAL_INDEX(line),
        VAL_SPECIFIER(line),
        ARG(delimiter)
    )){
        return R_THROWN;
    }

    if (IS_NULLED(OUT) or not REF(tail))
        return OUT;

    assert(IS_TEXT(OUT));

    return rebValue("append", OUT, ARG(delimiter));
}


//
//  checksum: native [
//
//  "Computes a checksum, CRC, or hash."
//
//      data [binary!]
//          "Bytes to checksum"
//      /part
//      limit
//          "Length of data"
//      /tcp
//          "Returns an Internet TCP 16-bit checksum"
//      /secure
//          "Returns a cryptographically secure checksum"
//      /hash
//          "Returns a hash value"
//      size [integer!]
//          "Size of the hash table"
//      /method
//          "Method to use"
//      word [word!]
//          "Methods: SHA1 MD5 CRC32"
//      /key
//          "Returns keyed HMAC value"
//      key-value [binary! text!]
//          "Key to use"
//  ]
//
DECLARE_NATIVE(checksum)
{
    INCLUDE_PARAMS_OF_CHECKSUM;

    Value* arg = ARG(data);

    REBLEN len = Part_Len_May_Modify_Index(arg, ARG(limit));
    UNUSED(REF(part)); // checked by if limit is nulled

    Byte *data = VAL_RAW_DATA_AT(arg); // after Partial() in case of change
    REBLEN wide = Series_Wide(VAL_SERIES(arg));

    SymId sym;
    if (REF(method)) {
        sym = try_unwrap(Cell_Word_Id(ARG(word)));
        if (sym == SYM_0) // not in %words.r, no SYM_XXX constant
            fail (Error_Invalid(ARG(word)));
    }
    else
        sym = SYM_SHA1;

    // If method, secure, or key... find matching digest:
    if (REF(method) || REF(secure) || REF(key)) {
        if (sym == SYM_CRC32) {
            if (REF(secure) || REF(key))
                fail (Error_Bad_Refines_Raw());

            // CRC32 is typically an unsigned 32-bit number and uses the full
            // range of values.  Yet Rebol chose to export this as a signed
            // integer via CHECKSUM.  Perhaps (?) to generate a value that
            // could be used by Rebol2, as it only had 32-bit signed INTEGER!.
            //
            REBINT crc32 = cast(int32_t, crc32_z(0L, data, len));
            return Init_Integer(OUT, crc32);
        }

        if (sym == SYM_ADLER32) {
            if (REF(secure) || REF(key))
                fail (Error_Bad_Refines_Raw());

            // adler32() is a Saphirion addition since 64-bit INTEGER! was
            // available in Rebol3, and did not convert the unsigned result
            // of the adler calculation to a signed integer.
            //
            uLong adler = z_adler32(0L, data, len);
            return Init_Integer(OUT, adler);
        }

        REBLEN i;
        for (i = 0; i != sizeof(digests) / sizeof(digests[0]); i++) {
            if (digests[i].sym != sym)
                continue;

            Binary* digest = Make_Binary(digests[i].len + 1);

            if (not REF(key))
                digests[i].digest(data, len, Binary_Head(digest));
            else {
                Value* key = ARG(key_value);

                REBLEN blocklen = digests[i].hmacblock;

                Byte tmpdigest[20]; // size must be max of all digest[].len

                Byte *keycp;
                REBSIZ keylen;
                if (IS_BINARY(key)) {
                    keycp = Cell_Binary_At(key);
                    keylen = VAL_LEN_AT(key);
                }
                else {
                    assert(IS_TEXT(key));

                    REBSIZ offset;
                    Binary* temp = Temp_UTF8_At_Managed(
                        &offset, &keylen, key, VAL_LEN_AT(key)
                    );
                    PUSH_GC_GUARD(temp);
                    keycp = Binary_At(temp, offset);
                }

                if (keylen > blocklen) {
                    digests[i].digest(keycp, keylen, tmpdigest);
                    keycp = tmpdigest;
                    keylen = digests[i].len;
                }

                Byte ipad[64]; // size must be max of all digest[].hmacblock
                memset(ipad, 0, blocklen);
                memcpy(ipad, keycp, keylen);

                Byte opad[64]; // size must be max of all digest[].hmacblock
                memset(opad, 0, blocklen);
                memcpy(opad, keycp, keylen);

                REBLEN j;
                for (j = 0; j < blocklen; j++) {
                    ipad[j] ^= 0x36; // !!! why do people write this kind of
                    opad[j] ^= 0x5c; // thing without a comment? !!! :-(
                }

                char *ctx = ALLOC_N(char, digests[i].ctxsize());
                digests[i].init(ctx);
                digests[i].update(ctx,ipad,blocklen);
                digests[i].update(ctx, data, len);
                digests[i].final(tmpdigest,ctx);
                digests[i].init(ctx);
                digests[i].update(ctx,opad,blocklen);
                digests[i].update(ctx,tmpdigest,digests[i].len);
                digests[i].final(Binary_Head(digest),ctx);

                FREE_N(char, digests[i].ctxsize(), ctx);
            }

            Term_Binary_Len(digest, digests[i].len);
            return Init_Binary(OUT, digest);
        }

        fail (Error_Invalid(ARG(word)));
    }
    else if (REF(tcp)) {
        REBINT ipc = Compute_IPC(data, len);
        Init_Integer(OUT, ipc);
    }
    else if (REF(hash)) {
        REBINT sum = VAL_INT32(ARG(size));
        if (sum <= 1)
            sum = 1;

        REBINT hash = Hash_Bytes_Or_Uni(data, len, wide) % sum;
        Init_Integer(OUT, hash);
    }
    else
        Init_Integer(OUT, Compute_CRC24(data, len));

    return OUT;
}


//
//  deflate: native [
//
//  "Compress data using DEFLATE: https://en.wikipedia.org/wiki/DEFLATE"
//
//      return: [binary!]
//      data [binary! text!]
//          "If text, it will be UTF-8 encoded"
//      /part
//      limit
//          "Length of data (elements)"
//      /envelope
//          {Add an envelope with header plus checksum/size information}
//      format [word!]
//          {ZLIB (adler32, no size) or GZIP (crc32, uncompressed size)}
//  ]
//
DECLARE_NATIVE(deflate)
{
    INCLUDE_PARAMS_OF_DEFLATE;

    Value* data = ARG(data);

    REBLEN len = Part_Len_May_Modify_Index(data, ARG(limit));
    UNUSED(PAR(part)); // checked by if limit is nulled

    REBSIZ size;
    Byte *bp;
    if (IS_BINARY(data)) {
        bp = Cell_Binary_At(data);
        size = len; // width = sizeof(Byte), so limit = len
    }
    else {
        REBSIZ offset;
        Binary* temp = Temp_UTF8_At_Managed(&offset, &size, data, len);
        bp = Binary_At(temp, offset);
    }

    Symbol* envelope;
    if (not REF(envelope))
        envelope = Canon(SYM_NONE);  // Note: nullptr is gzip (for bootstrap)
    else {
        envelope = Cell_Word_Symbol(ARG(format));
        switch (Symbol_Id(envelope)) {
          case SYM_ZLIB:
          case SYM_GZIP:
            break;

          default:
            fail (Error_Invalid(ARG(format)));
        }
    }

    size_t compressed_size;
    void *compressed = Compress_Alloc_Core(
        &compressed_size,
        bp,
        size,
        envelope
    );

    return rebRepossess(compressed, compressed_size);
}


//
//  inflate: native [
//
//  "Decompresses DEFLATEd data: https://en.wikipedia.org/wiki/DEFLATE"
//
//      return: [binary!]
//      data [binary!]
//      /part
//      limit
//          "Length of compressed data (must match end marker)"
//      /max
//      bound
//          "Error out if result is larger than this"
//      /envelope
//          {Expect (and verify) envelope with header/CRC/size information}
//      format [word!]
//          {ZLIB, GZIP, or DETECT (for http://stackoverflow.com/a/9213826)}
//  ]
//
DECLARE_NATIVE(inflate)
{
    INCLUDE_PARAMS_OF_INFLATE;

    Value* data = ARG(data);

    REBINT max;
    if (REF(max)) {
        max = Int32s(ARG(bound), 1);
        if (max < 0)
            fail (Error_Invalid(ARG(bound)));
    }
    else
        max = -1;

    // v-- measured in bytes (length of a BINARY!)
    REBLEN len = Part_Len_May_Modify_Index(data, ARG(limit));
    UNUSED(REF(part)); // checked by if limit is nulled

    Symbol* envelope;
    if (not REF(envelope))
        envelope = Canon(SYM_NONE);  // Note: nullptr is gzip (for bootstrap)
    else {
        switch (Cell_Word_Id(ARG(format))) {
          case SYM_ZLIB:
          case SYM_GZIP:
          case SYM_DETECT:
            envelope = Cell_Word_Symbol(ARG(format));
            break;

          default:
            fail (Error_Invalid(ARG(format)));
        }
    }

    size_t decompressed_size;
    void *decompressed = Decompress_Alloc_Core(
        &decompressed_size,
        Cell_Binary_At(data),
        len,
        max,
        envelope
    );

    return rebRepossess(decompressed, decompressed_size);
}


//
//  debase: native [
//
//  {Decodes binary-coded string (BASE-64 default) to binary value.}
//
//      return: [binary!]
//          ;-- Comment said "we don't know the encoding" of the return binary
//      value [binary! text!]
//          "The string to decode"
//      /base
//          "Binary base to use"
//      base-value [integer!]
//          "The base to convert from: 64, 16, or 2"
//  ]
//
DECLARE_NATIVE(debase)
{
    INCLUDE_PARAMS_OF_DEBASE;

    REBSIZ offset;
    REBSIZ size;
    Binary* temp = Temp_UTF8_At_Managed(
        &offset, &size, ARG(value), VAL_LEN_AT(ARG(value))
    );

    REBINT base = 64;
    if (REF(base))
        base = VAL_INT32(ARG(base_value));
    else
        base = 64;

    if (!Decode_Binary(OUT, Binary_At(temp, offset), size, base, 0))
        fail (Error_Invalid_Data_Raw(ARG(value)));

    return OUT;
}


//
//  enbase: native [
//
//  {Encodes data into a binary, hexadecimal, or base-64 ASCII string.}
//
//      return: [text!]
//      value [binary! text!]
//          "If text, will be UTF-8 encoded"
//      /base
//          "Binary base to use (BASE-64 default)"
//      base-value [integer!]
//          "The base to convert to: 64, 16, or 2"
//  ]
//
DECLARE_NATIVE(enbase)
{
    INCLUDE_PARAMS_OF_ENBASE;

    REBINT base;
    if (REF(base))
        base = VAL_INT32(ARG(base_value));
    else
        base = 64;

    Value* v = ARG(value);

    REBSIZ size;
    Byte *bp;
    if (IS_BINARY(v)) {
        bp = Cell_Binary_At(v);
        size = VAL_LEN_AT(v);
    }
    else { // Convert the string to UTF-8
        assert(ANY_STRING(v));
        REBSIZ offset;
        Binary* temp = Temp_UTF8_At_Managed(&offset, &size, v, VAL_LEN_AT(v));
        bp = Binary_At(temp, offset);
    }

    Binary* enbased;
    const bool brk = false;
    switch (base) {
    case 64:
        enbased = Encode_Base64(bp, size, brk);
        break;

    case 16:
        enbased = Encode_Base16(bp, size, brk);
        break;

    case 2:
        enbased = Encode_Base2(bp, size, brk);
        break;

    default:
        fail (Error_Invalid(ARG(base_value)));
    }

    // !!! Enbasing code is common with how a BINARY! molds out.  That needed
    // the returned series to be UTF-8.  Once STRING! in Rebol is UTF-8 also,
    // then this conversion won't be necessary.

    Init_Text(
        OUT,
        Make_Sized_String_UTF8(cs_cast(Binary_Head(enbased)), Binary_Len(enbased))
    );
    Free_Unmanaged_Series(enbased);

    return OUT;
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

    REBLEN len = VAL_LEN_AT(ARG(string));

    DECLARE_MOLD (mo);
    Push_Mold (mo);

    // !!! For now, we conservatively assume that the mold buffer might need
    // 12x as many characters as the input.  This is based on the worst-case
    // scenario, that each single codepoint might need 4 bytes of UTF-8 data
    // that are turned into %XX%XX%XX%XX in the output stream.
    //
    // It's not that big a deal since the mold buffer sits around with a large
    // capacity anyway, so it probably has enough for the short encodings this
    // does already.  But after the UTF-8 everywhere conversion, molding logic
    // is smarter and expands the buffer on-demand so routines like this don't
    // need to preallocate it.
    //
    Byte *dp = Prep_Mold_Overestimated(mo, len * 12);

    Series* s = VAL_SERIES(ARG(string));

    REBLEN i = VAL_INDEX(ARG(string));
    for (; i < len; ++i) {
        REBUNI c = GET_ANY_CHAR(s, i);

        Byte encoded[4];
        REBLEN encoded_size;

        if (c > 0x80) // all non-ASCII characters *must* be percent encoded
            encoded_size = Encode_UTF8_Char(encoded, c);
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
                case LEX_DELIMIT_SEMICOLON:
                    goto leave_as_is;

                case LEX_DELIMIT_SPACE: // includes control characters
                case LEX_DELIMIT_END: // 00 null terminator
                case LEX_DELIMIT_LINEFEED:
                case LEX_DELIMIT_RETURN: // e.g. ^M
                case LEX_DELIMIT_LEFT_BRACE:
                case LEX_DELIMIT_RIGHT_BRACE:
                case LEX_DELIMIT_DOUBLE_QUOTE:
                    goto needs_encoding;

                case LEX_DELIMIT_UTF8_ERROR: // not for c < 0x80
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
                case LEX_SPECIAL_PERIOD:
                case LEX_SPECIAL_COMMA:
                case LEX_SPECIAL_POUND:
                case LEX_SPECIAL_DOLLAR:
                    goto leave_as_is;

                default:
                    goto needs_encoding; // what is LEX_SPECIAL_WORD?
                }
                goto leave_as_is;

            case LEX_CLASS_WORD:
                if (
                    (c >= 'a' and c <= 'z') or (c >= 'A' and c <= 'Z')
                    or c == '?' or c == '!' or c == '&'
                    or c == '*' or c == '=' or c == '~'
                ){
                    goto leave_as_is; // this is all that's leftover
                }
                goto needs_encoding;

            case LEX_CLASS_NUMBER:
                goto leave_as_is; // 0-9 needs no encoding.
            }

        leave_as_is:;
          #if !defined(NDEBUG)
            assert(strchr(no_encode, c) != nullptr);
          #endif
            *dp++ = c;
            continue;
        }

    needs_encoding:;
      #if !defined(NDEBUG)
        if (c < 0x80)
           assert(strchr(no_encode, c) == nullptr);
      #endif

        REBLEN n;
        for (n = 0; n != encoded_size; ++n) {
            *dp++ = '%';

            // Use uppercase hex digits, per RFC 3896 2.1, which is also
            // consistent with JavaScript's encodeURIComponent()
            //
            // https://tools.ietf.org/html/rfc3986#section-2.1
            //
            *dp++ = Hex_Digits[(encoded[n] & 0xf0) >> 4];
            *dp++ = Hex_Digits[encoded[n] & 0xf];
        }
    }

    *dp = '\0';

    Set_Series_Len(mo->series, dp - Binary_Head(mo->series));

    return Init_Any_Series(
        OUT,
        VAL_TYPE(ARG(string)),
        Pop_Molded_String(mo)
    );
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

    REBLEN len = VAL_LEN_AT(ARG(string));

    DECLARE_MOLD (mo);
    Push_Mold(mo);

    // Conservatively assume no %NNs, and output is same length as input, with
    // all codepoints expanding to 4 bytes.
    //
    Byte *dp = Prep_Mold_Overestimated(mo, len * 4);

    // RFC 3986 says the encoding/decoding must use UTF-8.  This temporary
    // buffer is used to hold up to 4 bytes (and a terminator) that need
    // UTF-8 decoding--the maximum one UTF-8 encoded codepoint may have.
    //
    Byte scan[5];
    REBSIZ scan_size = 0;

    Series* s = VAL_SERIES(ARG(string));

    REBLEN i = VAL_INDEX(ARG(string));

    REBUNI c = GET_ANY_CHAR(s, i);
    while (i < len) {

        if (c != '%') {
            dp += Encode_UTF8_Char(dp, c);
            ++i;
        }
        else {
            if (i + 2 >= len)
               fail ("Percent decode has less than two codepoints after %");

            Byte lex1 = Lex_Map[GET_ANY_CHAR(s, i + 1)];
            Byte lex2 = Lex_Map[GET_ANY_CHAR(s, i + 2)];
            i += 3;

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

        c = GET_ANY_CHAR(s, i); // may be '\0', guaranteed to be if `i == len`

        // If our scanning buffer is full (and hence should contain at *least*
        // one full codepoint) or there are no more UTF-8 bytes coming (due
        // to end of string or the next input not a %XX pattern), then try
        // to decode what we've got.
        //
        if (scan_size > 0 and (c != '%' or scan_size == 4)) {
            assert(i != len or c == '\0');

        decode_codepoint:
            scan[scan_size] = '\0';
            const Byte *next; // goto would cross initialization
            REBUNI decoded;
            if (scan[0] < 0x80) {
                decoded = scan[0];
                next = &scan[0]; // last byte is only byte (see Back_Scan)
            }
            else {
                next = Back_Scan_UTF8_Char(&decoded, scan, &scan_size);
                if (next == nullptr)
                    fail ("Bad UTF-8 sequence in %XX of dehex");
            }
            dp += Encode_UTF8_Char(dp, decoded);
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

    *dp = '\0';

    Set_Series_Len(mo->series, dp - Binary_Head(mo->series));

    return Init_Any_Series(
        OUT,
        VAL_TYPE(ARG(string)),
        Pop_Molded_String(mo)
    );
}


//
//  deline: native [
//
//  {Converts string terminators to standard format, e.g. CR LF to LF.}
//
//      return: [any-string! block!]
//      string [any-string!]
//          "Will be modified (unless /LINES used)"
//      /lines
//          {Return block of lines (works for LF, CR, CR-LF endings)}
//  ]
//
DECLARE_NATIVE(deline)
{
    INCLUDE_PARAMS_OF_DELINE;

    Value* val = ARG(string);

    if (REF(lines))
        return Init_Block(OUT, Split_Lines(val));

    String* s = Cell_String(val);
    REBLEN len_head = Series_Len(s);

    REBLEN len_at = VAL_LEN_AT(val);

    Ucs2(*) dest = Cell_String_At(val);
    Ucs2(const*) src = dest;

    REBLEN n;
    for (n = 0; n < len_at; ++n) {
        REBUNI c;
        src = Ucs2_Next(&c, src);
        ++n;
        if (c == CR) {
            dest = Write_Codepoint(dest, LF);
            src = Ucs2_Next(&c, src);
            ++n; // will see NUL terminator before loop check, so is safe
            if (c == LF) {
                --len_head; // don't write carraige return, note loss of char
                continue;
            }
        }
        dest = Write_Codepoint(dest, c);
    }

    Term_String_Len(s, len_head);

    RETURN (ARG(string));
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

    Value* val = ARG(string);

    String* ser = Cell_String(val);
    REBLEN idx = VAL_INDEX(val);
    REBLEN len = VAL_LEN_AT(val);

    REBLEN delta = 0;

    // Calculate the size difference by counting the number of LF's
    // that have no CR's in front of them.
    //
    // !!! The Ucs2(*) interface isn't technically necessary if one is
    // counting to the end (one could just go by bytes instead of characters)
    // but this would not work if someone added, say, an ENLINE/PART...since
    // the byte ending position of interest might not be end of the string.

    Ucs2(*) cp = String_At(ser, idx);

    REBUNI c_prev = '\0';

    REBLEN n;
    for (n = 0; n < len; ++n) {
        REBUNI c;
        cp = Ucs2_Next(&c, cp);
        if (c == LF and c_prev != CR)
            ++delta;
        c_prev = c;
    }

    if (delta == 0)
        RETURN (ARG(string)); // nothing to do

    Expand_Series_Tail(ser, delta);

    // !!! After the UTF-8 Everywhere conversion, this will be able to stay
    // a byte-oriented process..because UTF-8 doesn't reuse ASCII chars in
    // longer codepoints, and CR and LF are ASCII.  So as long as the
    // "sliding" is done in terms of byte sizes and not character lengths,
    // it should be all right.
    //
    // Prior to UTF-8 Everywhere, sliding can't be done bytewise, because
    // UCS-2 has the CR LF bytes in codepoint sequences that aren't CR LF.
    // So sliding is done in full character counts.

    REBUNI *up = String_Head(ser); // expand may change the pointer
    REBLEN tail = Series_Len(ser); // length after expansion

    // Add missing CRs

    while (delta > 0) {
        up[tail--] = up[len]; // Copy src to dst.
        if (up[len] == LF and (len == 0 or up[len - 1] != CR)) {
            up[tail--] = CR;
            --delta;
        }
        --len;
    }

    RETURN (ARG(string));
}


//
//  entab: native [
//
//  "Converts spaces to tabs (default tab size is 4)."
//
//      string [any-string!]
//          "(modified)"
//      /size
//          "Specifies the number of spaces per tab"
//      number [integer!]
//  ]
//
DECLARE_NATIVE(entab)
{
    INCLUDE_PARAMS_OF_ENTAB;

    Value* val = ARG(string);

    REBINT tabsize;
    if (REF(size))
        tabsize = Int32s(ARG(number), 1);
    else
        tabsize = TAB_SIZE;

    DECLARE_MOLD (mo);
    Push_Mold(mo);

    REBLEN len = VAL_LEN_AT(val);
    Byte *dp = Prep_Mold_Overestimated(mo, len * 4); // max UTF-8 charsize

    Ucs2(const*) up = Cell_String_At(val);
    REBLEN index = VAL_INDEX(val);

    REBINT n = 0;
    for (; index < len; index++) {
        REBUNI c;
        up = Ucs2_Next(&c, up);

        // Count leading spaces, insert TAB for each tabsize:
        if (c == ' ') {
            if (++n >= tabsize) {
                *dp++ = '\t';
                n = 0;
            }
            continue;
        }

        // Hitting a leading TAB resets space counter:
        if (c == '\t') {
            *dp++ = cast(Byte, c);
            n = 0;
        }
        else {
            // Incomplete tab space, pad with spaces:
            for (; n > 0; n--)
                *dp++ = ' ';

            // Copy chars thru end-of-line (or end of buffer):
            for (; index < len; ++index) {
                if (c == '\n') {
                    *dp = '\n';
                    break;
                }
                dp += Encode_UTF8_Char(dp, c);
                up = Ucs2_Next(&c, up);
            }
        }
    }

    Term_Binary_Len(mo->series, dp - Binary_Head(mo->series));

    return Init_Any_Series(OUT, VAL_TYPE(val), Pop_Molded_String(mo));
}


//
//  detab: native [
//
//  "Converts tabs to spaces (default tab size is 4)."
//
//      string [any-string!]
//          "(modified)"
//      /size
//          "Specifies the number of spaces per tab"
//      number [integer!]
//  ]
//
DECLARE_NATIVE(detab)
{
    INCLUDE_PARAMS_OF_DETAB;

    Value* val = ARG(string);

    REBLEN len = VAL_LEN_AT(val);

    REBINT tabsize;
    if (REF(size))
        tabsize = Int32s(ARG(number), 1);
    else
        tabsize = TAB_SIZE;

    DECLARE_MOLD (mo);

    // Estimate new length based on tab expansion:

    Ucs2(const*) cp = Cell_String_At(val);
    REBLEN index = VAL_INDEX(val);

    REBLEN count = 0;
    REBLEN n;
    for (n = index; n < len; n++) {
        REBUNI c;
        cp = Ucs2_Next(&c, cp);
        if (c == '\t') // tab character
            ++count;
    }

    Push_Mold(mo);

    Byte *dp = Prep_Mold_Overestimated(
        mo,
        (len * 4) // assume worst case, all characters encode UTF-8 4 bytes
            + (count * (tabsize - 1)) // expanded tabs add tabsize - 1 to len
    );

    cp = Cell_String_At(val);

    n = 0;
    for (; index < len; ++index) {
        REBUNI c;
        cp = Ucs2_Next(&c, cp);

        if (c == '\t') {
            *dp++ = ' ';
            n++;
            for (; n % tabsize != 0; n++)
                *dp++ = ' ';
            continue;
        }

        if (c == '\n')
            n = 0;
        else
            ++n;

        dp += Encode_UTF8_Char(dp, c);
    }

    Term_Binary_Len(mo->series, dp - Binary_Head(mo->series));

    return Init_Any_Series(OUT, VAL_TYPE(val), Pop_Molded_String(mo));
}


//
//  lowercase: native [
//
//  "Converts string of characters to lowercase."
//
//      string [any-string! char!]
//          "(modified if series)"
//      /part
//          "Limits to a given length or position"
//      limit [any-number! any-string!]
//  ]
//
DECLARE_NATIVE(lowercase)
{
    INCLUDE_PARAMS_OF_LOWERCASE;

    UNUSED(REF(part)); // checked by if limit is null
    Change_Case(OUT, ARG(string), ARG(limit), false);
    return OUT;
}


//
//  uppercase: native [
//
//  "Converts string of characters to uppercase."
//
//      string [any-string! char!]
//          "(modified if series)"
//      /part
//          "Limits to a given length or position"
//      limit [any-number! any-string!]
//  ]
//
DECLARE_NATIVE(uppercase)
{
    INCLUDE_PARAMS_OF_UPPERCASE;

    UNUSED(REF(part)); // checked by if limit is nulled
    Change_Case(OUT, ARG(string), ARG(limit), true);
    return OUT;
}


//
//  to-hex: native [
//
//  {Converts numeric value to a hex issue! datatype (with leading # and 0's).}
//
//      value [integer! tuple!]
//          "Value to be converted"
//      /size
//          "Specify number of hex digits in result"
//      len [integer!]
//  ]
//
DECLARE_NATIVE(to_hex)
{
    INCLUDE_PARAMS_OF_TO_HEX;

    Value* arg = ARG(value);

    Byte buffer[(MAX_TUPLE * 2) + 4];  // largest value possible

    Byte *buf = &buffer[0];

    REBINT len;
    if (REF(size)) {
        len = cast(REBINT, VAL_INT64(ARG(len)));
        if (len < 0)
            fail (Error_Invalid(ARG(len)));
    }
    else
        len = -1;

    if (IS_INTEGER(arg)) {
        if (len < 0 || len > MAX_HEX_LEN)
            len = MAX_HEX_LEN;

        Form_Hex_Pad(buf, VAL_INT64(arg), len);
    }
    else if (IS_TUPLE(arg)) {
        REBINT n;
        if (
            len < 0
            || len > 2 * cast(REBINT, MAX_TUPLE)
            || len > 2 * VAL_TUPLE_LEN(arg)
        ){
            len = 2 * VAL_TUPLE_LEN(arg);
        }
        for (n = 0; n != VAL_TUPLE_LEN(arg); n++)
            buf = Form_Hex2_UTF8(buf, VAL_TUPLE(arg)[n]);
        for (; n < 3; n++)
            buf = Form_Hex2_UTF8(buf, 0);
        *buf = 0;
    }
    else
        fail (Error_Invalid(arg));

    Erase_Cell(OUT);
    if (nullptr == Scan_Issue(OUT, &buffer[0], len))
        fail (Error_Invalid(arg));

    return OUT;
}


//
//  find-script: native [
//
//  {Find a script header within a binary string. Returns starting position.}
//
//      return: [~null~ binary!]
//      script [binary!]
//  ]
//
DECLARE_NATIVE(find_script)
{
    INCLUDE_PARAMS_OF_FIND_SCRIPT;

    Value* arg = ARG(script);

    REBINT offset = Scan_Header(Cell_Binary_At(arg), VAL_LEN_AT(arg));
    if (offset == -1)
        return nullptr;

    Copy_Cell(OUT, arg);
    VAL_INDEX(OUT) += offset;
    return OUT;
}


//
//  invalid-utf8?: native [
//
//  {Checks UTF-8 encoding; if correct, returns null else position of error.}
//
//      data [binary!]
//  ]
//
DECLARE_NATIVE(invalid_utf8_q)
{
    INCLUDE_PARAMS_OF_INVALID_UTF8_Q;

    Value* arg = ARG(data);

    Byte *bp = Check_UTF8(Cell_Binary_At(arg), VAL_LEN_AT(arg));
    if (not bp)
        return nullptr;

    Copy_Cell(OUT, arg);
    VAL_INDEX(OUT) = bp - Cell_Binary_Head(arg);
    return OUT;
}
