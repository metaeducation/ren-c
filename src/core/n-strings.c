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
// Copyright 2012-2016 Rebol Open Source Contributors
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
    // REBYTE *SHA1(REBYTE *, REBCNT, REBYTE *);
    #ifdef __cplusplus
    extern "C" {
    #endif

    void SHA1_Init(void *c);
    void SHA1_Update(void *c, REBYTE *data, REBCNT len);
    void SHA1_Final(REBYTE *md, void *c);
    int  SHA1_CtxSize(void);

    #ifdef __cplusplus
    }
    #endif
    #endif

#if !defined(MD5_DEFINED) && defined(HAS_MD5)
    #ifdef __cplusplus
    extern "C" {
    #endif

    void MD5_Init(void *c);
    void MD5_Update(void *c, REBYTE *data, REBCNT len);
    void MD5_Final(REBYTE *md, void *c);
    int  MD5_CtxSize(void);

    #ifdef __cplusplus
    }
    #endif
#endif

#ifdef HAS_MD4
    REBYTE *MD4(REBYTE *, REBCNT, REBYTE *);

    #ifdef __cplusplus
    extern "C" {
    #endif

    void MD4_Init(void *c);
    void MD4_Update(void *c, REBYTE *data, REBCNT len);
    void MD4_Final(REBYTE *md, void *c);
    int  MD4_CtxSize(void);

    #ifdef __cplusplus
    }
    #endif
#endif


// Table of has functions and parameters:
static struct {
    REBYTE *(*digest)(REBYTE *, REBCNT, REBYTE *);
    void (*init)(void *);
    void (*update)(void *, REBYTE *, REBCNT);
    void (*final)(REBYTE *, void *);
    int (*ctxsize)(void);
    REBSYM sym;
    REBINT len;
    REBINT hmacblock;
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

    {NULL, NULL, NULL, NULL, NULL, SYM_0, 0, 0}

};


//
//  ajoin: native [
//  
//  {Reduces and joins a block of values into a new string.}
//  
//      block [block!]
//  ]
//
REBNATIVE(ajoin)
{
    PARAM(1, block);

    REBVAL *block = ARG(block);

    if (Form_Reduce_Throws(
        D_OUT, VAL_ARRAY(block), VAL_INDEX(block), VAL_SPECIFIER(block)
    )) {
        return R_OUT_IS_THROWN;
    }

    return R_OUT;
}


//
//  spelling-of: native [
//  
//  {Gives the delimiter-less spelling of words or strings}
//  
//      value [any-word! any-string!]
//  ]
//
REBNATIVE(spelling_of)
//
// This is a native implementation of SPELLING-OF from rebol-proposals.
{
    PARAM(1, value);

    REBVAL *value = ARG(value);

    REBSER *series;

    if (ANY_BINSTR(value)) {
        assert(!IS_BINARY(value)); // Shouldn't accept binary types...

        // Grab the data out of all string types, which has no delimiters
        // included (they are added in the forming process)
        //
        series = Copy_String_Slimming(VAL_SERIES(value), VAL_INDEX(value), -1);
    }
    else {
        // turn all words into regular words so they'll have no delimiters
        // during the FORMing process.  Use SET_TYPE and not reset header
        // because the binding bits need to stay consistent
        //
        VAL_SET_TYPE_BITS(value, REB_WORD);
        series = Copy_Mold_Value(value, 0 /* opts... MOPT_0? */);
    }

    Val_Init_String(D_OUT, series);
    return R_OUT;
}


//
//  checksum: native [
//  
//  "Computes a checksum, CRC, or hash."
//  
//      data [binary!] "Bytes to checksum"
//      /part limit "Length of data"
//      /tcp "Returns an Internet TCP 16-bit checksum"
//      /secure "Returns a cryptographically secure checksum"
//      /hash "Returns a hash value"
//      size [integer!] "Size of the hash table"
//      /method "Method to use"
//      word [word!] "Methods: SHA1 MD5 CRC32"
//      /key "Returns keyed HMAC value"
//      key-value [any-string!] "Key to use"
//  ]
//
REBNATIVE(checksum)
{
    REBVAL *arg = D_ARG(ARG_CHECKSUM_DATA);
    REBYTE *data = VAL_RAW_DATA_AT(arg);
    REBCNT wide = SER_WIDE(VAL_SERIES(arg));
    REBCNT len = Partial1(arg, D_ARG(ARG_CHECKSUM_SIZE));
    REBSYM sym = SYM_SHA1;

    // Method word:
    if (D_REF(ARG_CHECKSUM_METHOD)) {
        sym = VAL_WORD_SYM(D_ARG(ARG_CHECKSUM_WORD));
        if (sym == SYM_0) // not in %words.r, no SYM_XXX constant
            fail (Error_Invalid_Arg(D_ARG(ARG_CHECKSUM_WORD)));
    }

    // If method, secure, or key... find matching digest:
    if (D_REF(ARG_CHECKSUM_METHOD) || D_REF(ARG_CHECKSUM_SECURE) || D_REF(ARG_CHECKSUM_KEY)) {
        REBCNT i;

        if (sym == SYM_CRC32) {
            // The CRC32() routine returns an unsigned 32-bit number and uses
            // the full range of values.  Yet Rebol chose to export this as
            // a signed integer via checksum.  Perhaps (?) to generate a value
            // that could also be used by Rebol2, as it only had 32-bit
            // signed INTEGER! available.

            REBINT crc32;
            if (D_REF(ARG_CHECKSUM_SECURE) || D_REF(ARG_CHECKSUM_KEY))
                fail (Error(RE_BAD_REFINES));
            crc32 = cast(REBINT, CRC32(data, len));
            SET_INTEGER(D_OUT, crc32);
            return R_OUT;
        }

        if (sym == SYM_ADLER32) {
            // adler32() is a Saphirion addition since 64-bit INTEGER! was
            // available in Rebol3, and did not convert the unsigned result
            // of the adler calculation to a signed integer.

            uLong adler = z_adler32(0L, data, len);
            if (D_REF(ARG_CHECKSUM_SECURE) || D_REF(ARG_CHECKSUM_KEY))
                fail (Error(RE_BAD_REFINES));
            SET_INTEGER(D_OUT, adler);
            return R_OUT;
        }

        for (i = 0; i < sizeof(digests) / sizeof(digests[0]); i++) {

            if (SAME_SYM_NONZERO(digests[i].sym, sym)) {
                REBSER *digest = Make_Series(
                    digests[i].len + 1, sizeof(char), MKS_NONE
                );

                if (D_REF(ARG_CHECKSUM_KEY)) {
                    REBYTE tmpdigest[20];       // Size must be max of all digest[].len;
                    REBYTE ipad[64],opad[64];   // Size must be max of all digest[].hmacblock;
                    char *ctx = ALLOC_N(char, digests[i].ctxsize());
                    REBVAL *key = D_ARG(ARG_CHECKSUM_KEY_VALUE);
                    REBYTE *keycp = VAL_BIN_AT(key);
                    int keylen = VAL_LEN_AT(key);
                    int blocklen = digests[i].hmacblock;
                    REBINT j;

                    if (keylen > blocklen) {
                        digests[i].digest(keycp,keylen,tmpdigest);
                        keycp = tmpdigest;
                        keylen = digests[i].len;
                    }

                    memset(ipad, 0, blocklen);
                    memset(opad, 0, blocklen);
                    memcpy(ipad, keycp, keylen);
                    memcpy(opad, keycp, keylen);

                    for (j = 0; j < blocklen; j++) {
                        ipad[j]^=0x36;
                        opad[j]^=0x5c;
                    }

                    digests[i].init(ctx);
                    digests[i].update(ctx,ipad,blocklen);
                    digests[i].update(ctx, data, len);
                    digests[i].final(tmpdigest,ctx);
                    digests[i].init(ctx);
                    digests[i].update(ctx,opad,blocklen);
                    digests[i].update(ctx,tmpdigest,digests[i].len);
                    digests[i].final(BIN_HEAD(digest),ctx);

                    FREE_N(char, digests[i].ctxsize(), ctx);

                } else {
                    digests[i].digest(data, len, BIN_HEAD(digest));
                }

                SET_SERIES_LEN(digest, digests[i].len);
                TERM_SEQUENCE(digest);
                Val_Init_Binary(D_OUT, digest);

                return 0;
            }
        }

        fail (Error_Invalid_Arg(D_ARG(ARG_CHECKSUM_WORD)));
    }
    else if (D_REF(ARG_CHECKSUM_TCP)) { // /tcp
        REBINT ipc = Compute_IPC(data, len);
        SET_INTEGER(D_OUT, ipc);
    }
    else if (D_REF(ARG_CHECKSUM_HASH)) {  // /hash
        REBINT hash;
        REBINT sum = VAL_INT32(D_ARG(ARG_CHECKSUM_SIZE)); // /size
        if (sum <= 1) sum = 1;
        hash = Hash_String(data, len, wide) % sum;
        SET_INTEGER(D_OUT, hash);
    }
    else {
        REBINT crc = Compute_CRC(data, len);
        SET_INTEGER(D_OUT, crc);
    }

    return R_OUT;
}


//
//  compress: native [
//  
//  "Compresses a string series and returns it."
//  
//      data [binary! string!] "If string, it will be UTF8 encoded"
//      /part limit "Length of data (elements)"
//      /gzip "Use GZIP checksum"
//      /only {Do not store header or envelope information ("raw")}
//  ]
//
REBNATIVE(compress)
{
    const REBOOL gzip = D_REF(4);
    const REBOOL only = D_REF(5);
    REBSER *ser;
    REBCNT index;
    REBCNT len;

    len = Partial1(D_ARG(1), D_ARG(3));

    ser = Temp_Bin_Str_Managed(D_ARG(1), &index, &len);

    Val_Init_Binary(D_OUT, Compress(ser, index, len, gzip, only));

    return R_OUT;
}


//
//  decompress: native [
//  
//  "Decompresses data. Result is binary."
//  
//      data [binary!] "Data to decompress"
//      /part lim "Length of compressed data (must match end marker)"
//      /gzip "Use GZIP checksum"
//      /limit size "Error out if result is larger than this"
//      /only {Do not look for header or envelope information ("raw")}
//  ]
//
REBNATIVE(decompress)
{
    REBVAL *arg = D_ARG(1);
    REBCNT len;
    REBOOL gzip = D_REF(4);
    REBOOL limit = D_REF(5);
    REBINT max = limit ? Int32s(D_ARG(6), 1) : -1;
    REBOOL only = D_REF(7);

    len = Partial1(D_ARG(1), D_ARG(3));

    // This truncation rule used to be in Decompress, which passed len
    // in as an extra parameter.  This was the only call that used it.
    if (len > BIN_LEN(VAL_SERIES(arg)))
        len = BIN_LEN(VAL_SERIES(arg));

    if (limit && max < 0)
        return R_BLANK; // !!! Should negative limit be an error instead?

    Val_Init_Binary(D_OUT, Decompress(
        BIN_HEAD(VAL_SERIES(arg)) + VAL_INDEX(arg), len, max, gzip, only
    ));

    return R_OUT;
}


//
//  debase: native [
//  
//  {Decodes binary-coded string (BASE-64 default) to binary value.}
//  
//      value [binary! string!] "The string to decode"
//      /base "Binary base to use"
//      base-value [integer!] "The base to convert from: 64, 16, or 2"
//  ]
//
REBNATIVE(debase)
//
// BINARY is returned. We don't know the encoding.
{
    REBINT base = 64;
    REBSER *ser;
    REBCNT index;
    REBCNT len = 0;

    ser = Temp_Bin_Str_Managed(D_ARG(1), &index, &len);

    if (D_REF(2)) base = VAL_INT32(D_ARG(3)); // /base

    if (!Decode_Binary(D_OUT, BIN_AT(ser, index), len, base, 0))
        fail (Error(RE_INVALID_DATA, D_ARG(1)));

    return R_OUT;
}


//
//  enbase: native [
//  
//  {Encodes a string into a binary-coded string (BASE-64 default).}
//  
//      value [binary! string!] "If string, will be UTF8 encoded"
//      /base "Binary base to use"
//      base-value [integer!] "The base to convert to: 64, 16, or 2"
//  ]
//
REBNATIVE(enbase)
{
    REBINT base = 64;
    REBSER *ser;
    REBCNT index;
    REBVAL *arg = D_ARG(1);
		REBSER *temp;

    temp = Temp_Bin_Str_Managed(arg, &index, NULL);
    Val_Init_Binary(arg, temp);
    VAL_INDEX(arg) = index;

    if (D_REF(2)) base = VAL_INT32(D_ARG(3));

    switch (base) {
    case 64:
        ser = Encode_Base64(arg, 0, FALSE);
        break;
    case 16:
        ser = Encode_Base16(arg, 0, FALSE);
        break;
    case 2:
        ser = Encode_Base2(arg, 0, FALSE);
        break;
    default:
        fail (Error_Invalid_Arg(D_ARG(3)));
    }

    Val_Init_String(D_OUT, ser);

    return R_OUT;
}


//
//  decloak: native [
//  
//  {Decodes a binary string scrambled previously by encloak.}
//  
//      data [binary!] "Binary series to descramble (modified)"
//      key [string! binary! integer!] "Encryption key or pass phrase"
//      /with "Use a string! key as-is (do not generate hash)"
//  ]
//
REBNATIVE(decloak)
{
    PARAM(1, data);
    PARAM(2, key);
    REFINE(3, with);

    if (!Cloak(
        TRUE,
        VAL_BIN_AT(ARG(data)),
        VAL_LEN_AT(ARG(data)),
        cast(REBYTE*, ARG(key)),
        0,
        REF(with)
    )) {
        fail (Error_Invalid_Arg(ARG(key)));
    }

    *D_OUT = *ARG(data);
    return R_OUT;
}


//
//  encloak: native [
//  
//  "Scrambles a binary string based on a key."
//  
//      data [binary!] "Binary series to scramble (modified)"
//      key [string! binary! integer!] "Encryption key or pass phrase"
//      /with "Use a string! key as-is (do not generate hash)"
//  ]
//
REBNATIVE(encloak)
{
    PARAM(1, data);
    PARAM(2, key);
    REFINE(3, with);

    if (!Cloak(
        FALSE,
        VAL_BIN_AT(ARG(data)),
        VAL_LEN_AT(ARG(data)),
        cast(REBYTE*, ARG(key)),
        0,
        REF(with))
    ) {
        fail (Error_Invalid_Arg(ARG(key)));
    }

    *D_OUT = *ARG(data);
    return R_OUT;
}


//
//  dehex: native [
//  
//  "Converts URL-style hex encoded (%xx) strings."
//  
//      value [any-string!] "The string to dehex"
//  ]
//
REBNATIVE(dehex)
{
    PARAM(1, value);

    REBCNT len = VAL_LEN_AT(ARG(value));
    REBUNI uni;
    REBSER *ser;

    if (VAL_BYTE_SIZE(ARG(value))) {
        REBYTE *bp = VAL_BIN_AT(ARG(value));
        REBYTE *dp = Reset_Buffer(BYTE_BUF, len);

        for (; len > 0; len--) {
            if (*bp == '%' && len > 2 && Scan_Hex2(bp + 1, &uni, FALSE)) {
                *dp++ = cast(REBYTE, uni);
                bp += 3;
                len -= 2;
            }
            else *dp++ = *bp++;
        }

        *dp = '\0';
        ser = Copy_String_Slimming(BYTE_BUF, 0, dp - BIN_HEAD(BYTE_BUF));
    }
    else {
        REBUNI *up = VAL_UNI_AT(ARG(value));
        REBUNI *dp;
        REB_MOLD mo;
        CLEARS(&mo);

        Push_Mold(&mo);

        // Do a conservative expansion, assuming there are no %NNs in the
        // series and the output string will be the same length as input
        //
        Expand_Series(mo.series, mo.start, len);

        dp = UNI_AT(mo.series, mo.start); // Expand_Series may change pointer

        for (; len > 0; len--) {
            if (
                *up == '%'
                && len > 2
                && Scan_Hex2(cast(REBYTE*, up + 1), dp, TRUE)
            ) {
                dp++;
                up += 3;
                len -= 2;
            }
            else *dp++ = *up++;
        }

        *dp = '\0';

        // The delta in dp from the original pointer position tells us the
        // actual size after the %NNs have been accounted for.
        //
        ser = Pop_Molded_String_Len(
            &mo, cast(REBCNT, dp - UNI_AT(mo.series, mo.start))
        );
    }

    Val_Init_Series(D_OUT, VAL_TYPE(ARG(value)), ser);

    return R_OUT;
}


//
//  deline: native [
//  
//  {Converts string terminators to standard format, e.g. CRLF to LF.}
//  
//      string [any-string!] "(modified)"
//      /lines 
//      {Return block of lines (works for LF, CR, CR-LF endings) (no modify)}
//  ]
//
REBNATIVE(deline)
{
    PARAM(1, string);
    REFINE(2, lines);

    REBVAL *val = ARG(string);
    REBINT len = VAL_LEN_AT(val);
    REBINT n;

    if (REF(lines)) {
        Val_Init_Block(D_OUT, Split_Lines(val));
        return R_OUT;
    }

    if (VAL_BYTE_SIZE(val)) {
        REBYTE *bp = VAL_BIN_AT(val);
        n = Deline_Bytes(bp, len);
    } else {
        REBUNI *up = VAL_UNI_AT(val);
        n = Deline_Uni(up, len);
    }

    SET_SERIES_LEN(VAL_SERIES(val), VAL_LEN_HEAD(val) - (len - n));

    *D_OUT = *ARG(string);
    return R_OUT;
}


//
//  enline: native [
//  
//  {Converts string terminators to native OS format, e.g. LF to CRLF.}
//  
//      series [any-string! block!] "(modified)"
//  ]
//
REBNATIVE(enline)
//
// Convert LF to CRLF or native format.
{
    PARAM(1, series);

    REBVAL *val = ARG(series);
    REBSER *ser = VAL_SERIES(val);

    if (SER_LEN(ser)) {
        if (VAL_BYTE_SIZE(val))
            Enline_Bytes(ser, VAL_INDEX(val), VAL_LEN_AT(val));
        else
            Enline_Uni(ser, VAL_INDEX(val), VAL_LEN_AT(val));
    }

    *D_OUT = *ARG(series);
    return R_OUT;
}


//
//  entab: native [
//  
//  "Converts spaces to tabs (default tab size is 4)."
//  
//      string [any-string!] "(modified)"
//      /size "Specifies the number of spaces per tab"
//      number [integer!]
//  ]
//
REBNATIVE(entab)
//
// Modifies input.
{
    REBVAL *val = D_ARG(1);
    REBINT tabsize = TAB_SIZE;
    REBSER *ser;
    REBCNT len = VAL_LEN_AT(val);

    if (D_REF(2)) tabsize = Int32s(D_ARG(3), 1);

    // Set up the copy buffer:
    if (VAL_BYTE_SIZE(val))
        ser = Entab_Bytes(VAL_BIN(val), VAL_INDEX(val), len, tabsize);
    else
        ser = Entab_Unicode(VAL_UNI(val), VAL_INDEX(val), len, tabsize);

    Val_Init_Series(D_OUT, VAL_TYPE(val), ser);

    return R_OUT;
}


//
//  detab: native [
//  
//  "Converts tabs to spaces (default tab size is 4)."
//  
//      string [any-string!] "(modified)"
//      /size "Specifies the number of spaces per tab"
//      number [integer!]
//  ]
//
REBNATIVE(detab)
{
    REBVAL *val = D_ARG(1);
    REBINT tabsize = TAB_SIZE;
    REBSER *ser;
    REBCNT len = VAL_LEN_AT(val);

    if (D_REF(2)) tabsize = Int32s(D_ARG(3), 1);

    // Set up the copy buffer:
    if (VAL_BYTE_SIZE(val))
        ser = Detab_Bytes(VAL_BIN(val), VAL_INDEX(val), len, tabsize);
    else
        ser = Detab_Unicode(VAL_UNI(val), VAL_INDEX(val), len, tabsize);

    Val_Init_Series(D_OUT, VAL_TYPE(val), ser);

    return R_OUT;
}


//
//  lowercase: native [
//  
//  "Converts string of characters to lowercase."
//  
//      string [any-string! char!] "(modified if series)"
//      /part "Limits to a given length or position"
//      limit [any-number! any-string!]
//  ]
//
REBNATIVE(lowercase)
{
    Change_Case(D_OUT, D_ARG(1), D_ARG(3), FALSE);
    return R_OUT;
}


//
//  uppercase: native [
//  
//  "Converts string of characters to uppercase."
//  
//      string [any-string! char!] "(modified if series)"
//      /part "Limits to a given length or position"
//      limit [any-number! any-string!]
//  ]
//
REBNATIVE(uppercase)
{
    Change_Case(D_OUT, D_ARG(1), D_ARG(3), TRUE);
    return R_OUT;
}


//
//  to-hex: native [
//  
//  {Converts numeric value to a hex issue! datatype (with leading # and 0's).}
//  
//      value [integer! tuple!] "Value to be converted"
//      /size "Specify number of hex digits in result"
//      len [integer!]
//  ]
//
REBNATIVE(to_hex)
{
    REBVAL *arg = D_ARG(1);
    REBINT len;
//  REBSER *series;
    REBYTE buffer[MAX_TUPLE*2+4];  // largest value possible
    REBYTE *buf;

    buf = &buffer[0];

    len = -1;
    if (D_REF(2)) { // /size
        len = (REBINT) VAL_INT64(D_ARG(3));
        if (len < 0) fail (Error_Invalid_Arg(D_ARG(3)));
    }
    if (IS_INTEGER(arg)) { // || IS_DECIMAL(arg)) {
        if (len < 0 || len > MAX_HEX_LEN) len = MAX_HEX_LEN;
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
        for (n = 0; n < VAL_TUPLE_LEN(arg); n++)
            buf = Form_Hex2(buf, VAL_TUPLE(arg)[n]);
        for (; n < 3; n++)
            buf = Form_Hex2(buf, 0);
        *buf = 0;
    }
    else
        fail (Error_Invalid_Arg(arg));

//  SER_LEN(series) = len;
//  Val_Init_Series(D_OUT, REB_ISSUE, series);
    Val_Init_Word(D_OUT, REB_ISSUE, Scan_Issue(&buffer[0], len));

    return R_OUT;
}


//
//  find-script: native [
//  
//  {Find a script header within a binary string. Returns starting position.}
//  
//      script [binary!]
//  ]
//
REBNATIVE(find_script)
{
    PARAM(1, script);

    REBVAL *arg = ARG(script);
    REBINT n;

    n = What_UTF(VAL_BIN_AT(arg), VAL_LEN_AT(arg));

    if (n != 0 && n != 8) return R_BLANK;  // UTF8 only

    if (n == 8) VAL_INDEX(arg) += 3;  // BOM8 length

    n = Scan_Header(VAL_BIN_AT(arg), VAL_LEN_AT(arg)); // returns offset

    if (n == -1) return R_BLANK;

    VAL_INDEX(arg) += n;

    *D_OUT = *ARG(script);
    return R_OUT;
}


//
//  utf?: native [
//  
//  {Returns UTF BOM (byte order marker) encoding; + for BE, - for LE.}
//  
//      data [binary!]
//  ]
//
REBNATIVE(utf_q)
{
    REBINT utf = What_UTF(VAL_BIN_AT(D_ARG(1)), VAL_LEN_AT(D_ARG(1)));
    SET_INTEGER(D_OUT, utf);
    return R_OUT;
}


//
//  invalid-utf?: native [
//  
//  {Checks UTF encoding; if correct, returns blank else position of error.}
//  
//      data [binary!]
//      /utf "Check encodings other than UTF-8"
//      num [integer!] "Bit size - positive for BE negative for LE"
//  ]
//
REBNATIVE(invalid_utf_q)
{
    PARAM(1, data);
    REFINE(2, utf);
    PARAM(3, num);

    REBVAL *arg = ARG(data);
    REBYTE *bp;

    bp = Check_UTF8(VAL_BIN_AT(arg), VAL_LEN_AT(arg));
    if (bp == 0) return R_BLANK;

    VAL_INDEX(arg) = bp - VAL_BIN_HEAD(arg);

    *D_OUT = *arg;
    return R_OUT;
}


#ifndef NDEBUG
//
//  b_cast_: C
// 
// Debug-only version of b_cast() that does type checking.
// If you get a complaint you probably meant to use cb_cast().
//
REBYTE *b_cast_(char *s)
{
    return cast(REBYTE *, s);
}


//
//  cb_cast_: C
// 
// Debug-only version of cb_cast() that does type checking.
// If you get a complaint you probably meant to use b_cast().
//
const REBYTE *cb_cast_(const char *s)
{
    return cast(const REBYTE *, s);
}


//
//  s_cast_: C
// 
// Debug-only version of s_cast() that does type checking.
// If you get a complaint you probably meant to use cs_cast().
//
char *s_cast_(REBYTE *s)
{
    return cast(char*, s);
}


//
//  cs_cast_: C
// 
// Debug-only version of cs_cast() that does type checking.
// If you get a complaint you probably meant to use s_cast().
//
const char *cs_cast_(const REBYTE *s)
{
    return cast(const char *, s);
}


//
//  COPY_BYTES_: C
// 
// Debug-only REBYTE-checked substitute for COPY_BYTES macro
// If you meant characters, consider if you wanted strncpy()
//
REBYTE *COPY_BYTES_(REBYTE *dest, const REBYTE *src, size_t count)
{
    return b_cast(strncpy(s_cast(dest), cs_cast(src), count));
}


//
//  LEN_BYTES_: C
// 
// Debug-only REBYTE-checked substitute for LEN_BYTES macro
// If you meant characters, consider if you wanted strlen()
//
size_t LEN_BYTES_(const REBYTE *str)
{
    return strlen(cs_cast(str));
}


//
//  COMPARE_BYTES_: C
// 
// Debug-only REBYTE-checked function for COMPARE_BYTES macro
// If you meant characters, consider if you wanted strcmp()
//
int COMPARE_BYTES_(const REBYTE *lhs, const REBYTE *rhs)
{
    return strcmp(cs_cast(lhs), cs_cast(rhs));
}


//
//  APPEND_BYTES_LIMIT_: C
// 
// REBYTE-checked function for APPEND_BYTES_LIMIT macro in Debug
// If you meant characters, you'll have to use strncat()/strlen()
// (there's no single <string.h> entry point for this purpose)
//
REBYTE *APPEND_BYTES_LIMIT_(REBYTE *dest, const REBYTE *src, size_t max)
{
    return b_cast(strncat(
        s_cast(dest), cs_cast(src), MAX(max - LEN_BYTES(dest) - 1, 0)
    ));
}


//
//  OS_STRNCPY_: C
// 
// Debug-only REBCHR-checked substitute for OS_STRNCPY macro
//
REBCHR *OS_STRNCPY_(REBCHR *dest, const REBCHR *src, size_t count)
{
#ifdef OS_WIDE_CHAR
    return cast(REBCHR*,
        wcsncpy(cast(wchar_t*, dest), cast(const wchar_t*, src), count)
    );
#else
    #ifdef TO_OPENBSD
        return cast(REBCHR*,
            strlcpy(cast(char*, dest), cast(const char*, src), count)
        );
    #else
        return cast(REBCHR*,
            strncpy(cast(char*, dest), cast(const char*, src), count)
        );
    #endif
#endif
}


//
//  OS_STRNCAT_: C
// 
// Debug-only REBCHR-checked function for OS_STRNCAT macro
//
REBCHR *OS_STRNCAT_(REBCHR *dest, const REBCHR *src, size_t max)
{
#ifdef OS_WIDE_CHAR
    return cast(REBCHR*,
        wcsncat(cast(wchar_t*, dest), cast(const wchar_t*, src), max)
    );
#else
    #ifdef TO_OPENBSD
        return cast(REBCHR*,
            strlcat(cast(char*, dest), cast(const char*, src), max)
        );
    #else
        return cast(REBCHR*,
            strncat(cast(char*, dest), cast(const char*, src), max)
        );
    #endif
#endif
}


//
//  OS_STRNCMP_: C
// 
// Debug-only REBCHR-checked substitute for OS_STRNCMP macro
//
int OS_STRNCMP_(const REBCHR *lhs, const REBCHR *rhs, size_t max)
{
#ifdef OS_WIDE_CHAR
    return wcsncmp(cast(const wchar_t*, lhs), cast(const wchar_t*, rhs), max);
#else
    return strncmp(cast(const char*, lhs), cast (const char*, rhs), max);
#endif
}


//
//  OS_STRLEN_: C
// 
// Debug-only REBCHR-checked substitute for OS_STRLEN macro
//
size_t OS_STRLEN_(const REBCHR *str)
{
#ifdef OS_WIDE_CHAR
    return wcslen(cast(const wchar_t*, str));
#else
    return strlen(cast(const char*, str));
#endif
}


//
//  OS_STRCHR_: C
// 
// Debug-only REBCHR-checked function for OS_STRCHR macro
//
REBCHR *OS_STRCHR_(const REBCHR *str, REBCNT ch)
{
    // We have to m_cast because C++ actually has a separate overloads of
    // wcschr and strchr which will return a const pointer if the in pointer
    // was const.
#ifdef OS_WIDE_CHAR
    return cast(REBCHR*,
        m_cast(wchar_t*, wcschr(cast(const wchar_t*, str), ch))
    );
#else
    return cast(REBCHR*,
        m_cast(char*, strchr(cast(const char*, str), ch))
    );
#endif
}


//
//  OS_MAKE_CH_: C
// 
// Debug-only REBCHR-checked function for OS_MAKE_CH macro
//
REBCHR OS_MAKE_CH_(REBCNT ch)
{
    REBCHR result;
    result.num = ch;
    return result;
}

#endif
