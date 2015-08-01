/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Module:  n-strings.c
**  Summary: native functions for strings
**  Section: natives
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"
#include "sys-deci-funcs.h"

#include "sys-zlib.h"

/***********************************************************************
**
**	Hash Function Externs
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
static struct digest {
	REBYTE *(*digest)(REBYTE *, REBCNT, REBYTE *);
	void (*init)(void *);
	void (*update)(void *, REBYTE *, REBCNT);
	void (*final)(REBYTE *, void *);
	int (*ctxsize)(void);
	REBINT index;
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

	{NULL, NULL, NULL, NULL, NULL, SYM_NOT_USED, 0, 0}

};


/***********************************************************************
**
*/	REBNATIVE(ajoin)
/*
***********************************************************************/
{
	REBSER *str;

	// !!! This uses stack pushing/popping protocol and doesn't need
	// to.  It's only called once; should probably be inlined here.
	// (Assuming AJOIN should exist, which it probably shouldn't.)

	str = Form_Reduce(VAL_SERIES(D_ARG(1)), VAL_INDEX(D_ARG(1)));
	if (!str) {
		DS_POP_INTO(D_OUT);
		return R_OUT;
	}

	Set_String(D_OUT, str); // not D_OUT (stack modified)

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(as_binary)
/*
***********************************************************************/
{
	Trap_DEAD_END(RE_DEPRECATED);
//	*D_OUT = *D_ARG(1);
//	VAL_SET(D_OUT, REB_BINARY);
	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(as_string)
/*
***********************************************************************/
{
	Trap_DEAD_END(RE_DEPRECATED);
//	*D_OUT = *D_ARG(1);
//	VAL_SET(D_OUT, REB_STRING);
	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(checksum)
/*
**		Computes checksum or hash value.
**
**		Note: Currently BINARY only.
**
**	Args:
**
**		data [any-string!] {Data to checksum}
**		/part length
**		/tcp {Returns an Internet TCP 16-bit checksum.}
**		/secure {Returns a cryptographically secure checksum.}
**		/hash {Returns a hash value}
**		size [integer!] {Size of the hash table}
**		/method {Method to use}
**		word [word!] {Method: SHA1 MD5}
**		/key {Returns keyed HMAC value}
**		key-value [any-string!] {Key to use}
**
***********************************************************************/
{
	REBVAL *arg = D_ARG(ARG_CHECKSUM_DATA);
	REBYTE *data = VAL_BIN_DATA(arg);
	REBCNT len = Partial1(arg, D_ARG(ARG_CHECKSUM_LENGTH));
	REBINT sym = SYM_SHA1;

	// Method word:
	if (D_REF(ARG_CHECKSUM_METHOD)) sym = VAL_WORD_CANON(D_ARG(ARG_CHECKSUM_WORD));

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
			if (D_REF(ARG_CHECKSUM_SECURE) || D_REF(ARG_CHECKSUM_KEY)) Trap_DEAD_END(RE_BAD_REFINES);
			crc32 = cast(REBINT, CRC32(data, len));
			SET_INTEGER(D_OUT, crc32);
			return R_OUT;
		}

		if (sym == SYM_ADLER32) {
			// adler32() is a Saphirion addition since 64-bit INTEGER! was
			// available in Rebol3, and did not convert the unsigned result
			// of the adler calculation to a signed integer.

			uLong adler = z_adler32(0L, data, len);
			if (D_REF(ARG_CHECKSUM_SECURE) || D_REF(ARG_CHECKSUM_KEY)) Trap_DEAD_END(RE_BAD_REFINES);
			SET_INTEGER(D_OUT, adler);
			return R_OUT;
		}

		for (i = 0; i < sizeof(digests) / sizeof(digests[0]); i++) {

			if (digests[i].index == sym) {
				REBSER *digest = Make_Series(
					digests[i].len, sizeof(char), FALSE
				);

				LABEL_SERIES(digest, "checksum digest");

				if (D_REF(ARG_CHECKSUM_KEY)) {
					REBYTE tmpdigest[20];		// Size must be max of all digest[].len;
					REBYTE ipad[64],opad[64];	// Size must be max of all digest[].hmacblock;
					char *ctx = ALLOC_ARRAY(char, digests[i].ctxsize());
					REBVAL *key = D_ARG(ARG_CHECKSUM_KEY_VALUE);
					REBYTE *keycp = VAL_BIN_DATA(key);
					int keylen = VAL_LEN(key);
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

					FREE_ARRAY(char, digests[i].ctxsize(), ctx);

				} else {
					digests[i].digest(data, len, BIN_HEAD(digest));
				}

				SERIES_TAIL(digest) = digests[i].len;
				Set_Series(REB_BINARY, D_OUT, digest);

				return 0;
			}
		}

		Trap_Arg_DEAD_END(D_ARG(ARG_CHECKSUM_WORD));
	}
	else if (D_REF(ARG_CHECKSUM_TCP)) { // /tcp
		REBINT ipc = Compute_IPC(data, len);
		SET_INTEGER(D_OUT, ipc);
	}
	else if (D_REF(ARG_CHECKSUM_HASH)) {  // /hash
		REBINT hash;
		REBINT sum = VAL_INT32(D_ARG(ARG_CHECKSUM_SIZE)); // /size
		if (sum <= 1) sum = 1;
		hash = Hash_String(data, len) % sum;
		SET_INTEGER(D_OUT, hash);
	}
	else {
		REBINT crc = Compute_CRC(data, len);
		SET_INTEGER(D_OUT, crc);
	}

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(compress)
/*
**		Binary and string (gets UTF8 converted).
**
***********************************************************************/
{
	REBSER *ser;
	REBCNT index;
	REBCNT len;

	len = Partial1(D_ARG(1), D_ARG(3));

	ser = Prep_Bin_Str(D_ARG(1), &index, &len); // result may be a SHARED BUFFER!

	Set_Binary(D_OUT, Compress(ser, index, len, D_REF(4))); // /gzip

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(decompress)
/*
**		Binary only.
**
***********************************************************************/
{
	REBVAL *arg = D_ARG(1);
	REBINT limit = 0;
	REBCNT len;
	REBOOL gzip = D_REF(4); // use gzip checksum

	len = Partial1(D_ARG(1), D_ARG(3));

	// This truncation rule used to be in Decompress, which passed len
	// in as an extra parameter.  This was the only call that used it.
	if (len > BIN_LEN(VAL_SERIES(arg)))
		len = BIN_LEN(VAL_SERIES(arg));

	if (D_REF(5)) limit = Int32s(D_ARG(6), 1); // /limit size
	if (limit < 0)
		return R_NONE; // !!! Should negative limit be an error instead?

	Set_Binary(D_OUT, Decompress(
		BIN_HEAD(VAL_SERIES(arg)) + VAL_INDEX(arg), len, limit, gzip
	));

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(construct)
/*
***********************************************************************/
{
	REBVAL *value = D_ARG(1);
	REBSER *parent = 0;
	REBSER *frame;

	if (IS_STRING(value) || IS_BINARY(value)) {
		REBCNT index;

		// Just a guess at size:
		frame = Make_Block(10);		// Use a std BUF_?
		Set_Block(D_OUT, frame);	// Keep safe

		// Convert string if necessary. Store back for safety.
		VAL_SERIES(value) = Prep_Bin_Str(value, &index, 0);

		// !issue! Is this what we really want here?
		Scan_Net_Header(frame, VAL_BIN(value) + index);
		value = D_OUT;
	}

	if (D_REF(2)) parent = VAL_OBJ_FRAME(D_ARG(3));

	frame = Construct_Object(parent, VAL_BLK_DATA(value), D_REF(4));
	SET_OBJECT(D_OUT, frame);

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(debase)
/*
**		Converts a binary base representation string to binary.
**		Input is a STRING, but BINARY is also accepted.
**		BINARY is returned. We don't know the encoding.
**
***********************************************************************/
{
	REBINT base = 64;
	REBSER *ser;
	REBCNT index;
	REBCNT len = 0;

	ser = Prep_Bin_Str(D_ARG(1), &index, &len); // result may be a SHARED BUFFER!

	if (D_REF(2)) base = VAL_INT32(D_ARG(3)); // /base

	if (!Decode_Binary(D_OUT, BIN_SKIP(ser, index), len, base, 0))
 		Trap1_DEAD_END(RE_INVALID_DATA, D_ARG(1));

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(enbase)
/*
**		Converts a binary to a binary base representation STRING.
**		Input is BINARY or STRING (UTF8 encoded).
**
***********************************************************************/
{
	REBINT base = 64;
	REBSER *ser;
	REBCNT index;
	REBVAL *arg = D_ARG(1);

	Set_Binary(arg, Prep_Bin_Str(arg, &index, 0)); // may be SHARED buffer
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
		Trap_Arg_DEAD_END(D_ARG(3));
	}

	Set_String(D_OUT, ser);

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(decloak)
/*
**		Input is BINARY only. Modifies input.
**
***********************************************************************/
{
	REBVAL *data = D_ARG(1);
	REBVAL *key  = D_ARG(2);

	if (!Cloak(TRUE, VAL_BIN_DATA(data), VAL_LEN(data), (REBYTE*)key, 0, D_REF(3)))
		Trap_Arg_DEAD_END(key);

	return R_ARG1;
}


/***********************************************************************
**
*/	REBNATIVE(encloak)
/*
**		Input is BINARY only. Modifies input.
**
***********************************************************************/
{
	REBVAL *data = D_ARG(1);
	REBVAL *key  = D_ARG(2);

	if (!Cloak(FALSE, VAL_BIN_DATA(data), VAL_LEN(data), (REBYTE*)key, 0, D_REF(3)))
		Trap_Arg_DEAD_END(key);

	return R_ARG1;
}


/***********************************************************************
**
*/	REBNATIVE(dehex)
/*
**		Works for any string.
**
***********************************************************************/
{
	REBVAL *arg = D_ARG(1);
	REBINT len = (REBINT)VAL_LEN(arg); // due to len -= 2 below
	REBUNI n;
	REBSER *ser;

	if (VAL_BYTE_SIZE(arg)) {
		REBYTE *bp = VAL_BIN_DATA(arg);
		REBYTE *dp = Reset_Buffer(BUF_FORM, len);

		for (; len > 0; len--) {
			if (*bp == '%' && len > 2 && Scan_Hex2(bp+1, &n, FALSE)) {
				*dp++ = (REBYTE)n;
				bp += 3;
				len -= 2;
			}
			else *dp++ = *bp++;
		}

		*dp = 0;
		ser = Copy_String(BUF_FORM, 0, dp - BIN_HEAD(BUF_FORM));
	}
	else {
		REBUNI *up = VAL_UNI_DATA(arg);
		REBUNI *dp = (REBUNI*)Reset_Buffer(BUF_MOLD, len);

		for (; len > 0; len--) {
			if (*up == '%' && len > 2 && Scan_Hex2((REBYTE*)(up+1), &n, TRUE)) {
				*dp++ = (REBUNI)n;
				up += 3;
				len -= 2;
			}
			else *dp++ = *up++;
		}

		*dp = 0;
		ser = Copy_String(BUF_MOLD, 0, dp - UNI_HEAD(BUF_MOLD));
	}

	Set_Series(VAL_TYPE(arg), D_OUT, ser);

	return R_OUT;
}


/***********************************************************************
**
*/  REBNATIVE(deline)
/*
**		Convert CR or CRLF strings to just LF strings.
**
***********************************************************************/
{
	REBVAL *val = D_ARG(1);
	REBINT len = VAL_LEN(val);
	REBINT n;

	if (D_REF(2)) { //lines
		Set_Block(D_OUT, Split_Lines(val));
		return R_OUT;
	}

	if (VAL_BYTE_SIZE(val)) {
		REBYTE *bp = VAL_BIN_DATA(val);
		n = Deline_Bytes(bp, len);
	} else {
		REBUNI *up = VAL_UNI_DATA(val);
		n = Deline_Uni(up, len);
	}

	VAL_TAIL(val) -= (len - n);

	return R_ARG1;
}


/***********************************************************************
**
*/  REBNATIVE(enline)
/*
**		Convert LF to CRLF or native format.
**
***********************************************************************/
{
	REBVAL *val = D_ARG(1);
	REBSER *ser = VAL_SERIES(val);

	if (SERIES_TAIL(ser)) {
		if (VAL_BYTE_SIZE(val))
			Enline_Bytes(ser, VAL_INDEX(val), VAL_LEN(val));
		else
			Enline_Uni(ser, VAL_INDEX(val), VAL_LEN(val));
	}

	return R_ARG1;
}


/***********************************************************************
**
*/  REBNATIVE(entab)
/*
**		Modifies input.
**
***********************************************************************/
{
	REBVAL *val = D_ARG(1);
	REBINT tabsize = TAB_SIZE;
	REBSER *ser;
	REBCNT len = VAL_LEN(val);

	if (D_REF(2)) tabsize = Int32s(D_ARG(3), 1);

	// Set up the copy buffer:
	if (VAL_BYTE_SIZE(val))
		ser = Entab_Bytes(VAL_BIN(val), VAL_INDEX(val), len, tabsize);
	else
		ser = Entab_Unicode(VAL_UNI(val), VAL_INDEX(val), len, tabsize);

	Set_Series(VAL_TYPE(val), D_OUT, ser);

	return R_OUT;
}


/***********************************************************************
**
*/  REBNATIVE(detab)
/*
***********************************************************************/
{
	REBVAL *val = D_ARG(1);
	REBINT tabsize = TAB_SIZE;
	REBSER *ser;
	REBCNT len = VAL_LEN(val);

	if (D_REF(2)) tabsize = Int32s(D_ARG(3), 1);

	// Set up the copy buffer:
	if (VAL_BYTE_SIZE(val))
		ser = Detab_Bytes(VAL_BIN(val), VAL_INDEX(val), len, tabsize);
	else
		ser = Detab_Unicode(VAL_UNI(val), VAL_INDEX(val), len, tabsize);

	Set_Series(VAL_TYPE(val), D_OUT, ser);

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(lowercase)
/*
***********************************************************************/
{
	Change_Case(D_OUT, D_ARG(1), D_ARG(3), FALSE);
	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(uppercase)
/*
***********************************************************************/
{
	Change_Case(D_OUT, D_ARG(1), D_ARG(3), TRUE);
	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(to_hex)
/*
***********************************************************************/
{
	REBVAL *arg = D_ARG(1);
	REBINT len;
//	REBSER *series;
	REBYTE buffer[MAX_TUPLE*2+4];  // largest value possible
	REBYTE *buf;

#ifdef removed
	if (IS_INTEGER(arg)) len = MAX_HEX_LEN;
	else if (IS_TUPLE(arg)) {
		len = VAL_TUPLE_LEN(arg);
		if (len < 3) len = 3;
		len *= 2;
	}
	else Trap_Arg_DEAD_END(arg);

	else if (IS_DECIMAL(arg)) len = MAX_HEX_LEN;
	else if (IS_MONEY(arg)) len = 24;
	else if (IS_CHAR(arg)) len = (VAL_CHAR(arg) > 0x7f) ? 4 : 2;
#endif

	buf = &buffer[0];

	len = -1;
	if (D_REF(2)) {	// /size
		len = (REBINT) VAL_INT64(D_ARG(3));
		if (len < 0) Trap_Arg_DEAD_END(D_ARG(3));
	}
	if (IS_INTEGER(arg)) { // || IS_DECIMAL(arg)) {
		if (len < 0 || len > MAX_HEX_LEN) len = MAX_HEX_LEN;
		Form_Hex_Pad(buf, VAL_INT64(arg), len);
	}
	else if (IS_TUPLE(arg)) {
		REBINT n;
		if (len < 0 || len > 2 * MAX_TUPLE || len > 2 * VAL_TUPLE_LEN(arg))
			len = 2 * VAL_TUPLE_LEN(arg);
		for (n = 0; n < VAL_TUPLE_LEN(arg); n++)
			buf = Form_Hex2(buf, VAL_TUPLE(arg)[n]);
		for (; n < 3; n++)
			buf = Form_Hex2(buf, 0);
		*buf = 0;
	}
	else Trap_Arg_DEAD_END(arg);

#ifdef removed
	else if (IS_CHAR(arg)) {
		REBSER *ser = Make_Binary(6);
		ser->tail = xEncode_UTF8_Char(BIN_HEAD(ser), VAL_CHAR(arg));
		for (len = 0; len < (signed)(ser->tail); len++)
			buf = Form_Hex2(buf, *BIN_SKIP(ser, len));
		len = ser->tail * 2;
		//Form_Hex_Pad(buf, VAL_CHAR(arg), len);
	}
	else if (IS_MONEY(arg)) {
		REBYTE tmp[12];
		deci_to_binary(&tmp[0], VAL_DECI(arg));
		for (len = 0; len < 12; len++)
			buf = Form_Hex2(buf, tmp[len]);
		len = 24;
	}
#endif

//	SERIES_TAIL(series) = len;
//	Set_Series(REB_ISSUE, D_OUT, series);
	Init_Word_Unbound(D_OUT, REB_ISSUE, Scan_Issue(&buffer[0], len));

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(find_script)
/*
***********************************************************************/
{
	REBVAL *arg = D_ARG(1);
	REBINT n;

	n = What_UTF(VAL_BIN_DATA(arg), VAL_LEN(arg));

	if (n != 0 && n != 8) return R_NONE;  // UTF8 only

	if (n == 8) VAL_INDEX(arg) += 3;  // BOM8 length

	n = Scan_Header(VAL_BIN_DATA(arg), VAL_LEN(arg)); // returns offset

	if (n == -1) return R_NONE;

	VAL_INDEX(arg) += n;

	return R_ARG1;
}


/***********************************************************************
**
*/	REBNATIVE(utfq)
/*
***********************************************************************/
{
	REBINT utf = What_UTF(VAL_BIN_DATA(D_ARG(1)), VAL_LEN(D_ARG(1)));
	SET_INTEGER(D_OUT, utf);
	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(invalid_utfq)
/*
***********************************************************************/
{
	REBVAL *arg = D_ARG(1);
	REBYTE *bp;

	bp = Check_UTF8(VAL_BIN_DATA(arg), VAL_LEN(arg));
	if (bp == 0) return R_NONE;

	VAL_INDEX(arg) = bp - VAL_BIN_HEAD(arg);
	return R_ARG1;
}


#ifndef NDEBUG
/***********************************************************************
**
*/	REBYTE *b_cast_(char *s)
/*
**		Debug-only version of b_cast() that does type checking.
**		If you get a complaint you probably meant to use cb_cast().
**
***********************************************************************/
{
	return cast(REBYTE *, s);
}


/***********************************************************************
**
*/	const REBYTE *cb_cast_(const char *s)
/*
**		Debug-only version of cb_cast() that does type checking.
**		If you get a complaint you probably meant to use b_cast().
**
***********************************************************************/
{
	return cast(const REBYTE *, s);
}


/***********************************************************************
**
*/	char *s_cast_(REBYTE *s)
/*
**		Debug-only version of s_cast() that does type checking.
**		If you get a complaint you probably meant to use cs_cast().
**
***********************************************************************/
{
	return cast(char*, s);
}


/***********************************************************************
**
*/	const char *cs_cast_(const REBYTE *s)
/*
**		Debug-only version of cs_cast() that does type checking.
**		If you get a complaint you probably meant to use s_cast().
**
***********************************************************************/
{
	return cast(const char *, s);
}


/***********************************************************************
**
*/	REBYTE *COPY_BYTES_(REBYTE *dest, const REBYTE *src, size_t count)
/*
**		Debug-only REBYTE-checked substitute for COPY_BYTES macro
**		If you meant characters, consider if you wanted strncpy()
**
***********************************************************************/
{
	return b_cast(strncpy(s_cast(dest), cs_cast(src), count));
}


/***********************************************************************
**
*/	size_t LEN_BYTES_(const REBYTE *str)
/*
**		Debug-only REBYTE-checked substitute for LEN_BYTES macro
**		If you meant characters, consider if you wanted strlen()
**
***********************************************************************/
{
	return strlen(cs_cast(str));
}


/***********************************************************************
**
*/	int COMPARE_BYTES_(const REBYTE *lhs, const REBYTE *rhs)
/*
**		Debug-only REBYTE-checked function for COMPARE_BYTES macro
**		If you meant characters, consider if you wanted strcmp()
**
***********************************************************************/
{
	return strcmp(cs_cast(lhs), cs_cast(rhs));
}


/***********************************************************************
**
*/	REBYTE *APPEND_BYTES_LIMIT_(REBYTE *dest, const REBYTE *src, size_t max)
/*
**		REBYTE-checked function for APPEND_BYTES_LIMIT macro in Debug
**		If you meant characters, you'll have to use strncat()/strlen()
**		(there's no single <string.h> entry point for this purpose)
**
***********************************************************************/
{
	return b_cast(strncat(
		s_cast(dest), cs_cast(src), MAX(max - LEN_BYTES(dest) - 1, 0)
	));
}


/***********************************************************************
**
*/	REBCHR *OS_STRNCPY_(REBCHR *dest, const REBCHR *src, size_t count)
/*
**		Debug-only REBCHR-checked substitute for OS_STRNCPY macro
**
***********************************************************************/
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


/***********************************************************************
**
*/	REBCHR *OS_STRNCAT_(REBCHR *dest, const REBCHR *src, size_t max)
/*
**		Debug-only REBCHR-checked function for OS_STRNCAT macro
**
***********************************************************************/
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


/***********************************************************************
**
*/	int OS_STRNCMP_(const REBCHR *lhs, const REBCHR *rhs, size_t max)
/*
**		Debug-only REBCHR-checked substitute for OS_STRNCMP macro
**
***********************************************************************/
{
#ifdef OS_WIDE_CHAR
	return wcsncmp(cast(const wchar_t*, lhs), cast(const wchar_t*, rhs), max);
#else
	return strncmp(cast(const char*, lhs), cast (const char*, rhs), max);
#endif
}


/***********************************************************************
**
*/	size_t OS_STRLEN_(const REBCHR *str)
/*
**		Debug-only REBCHR-checked substitute for OS_STRLEN macro
**
***********************************************************************/
{
#ifdef OS_WIDE_CHAR
	return wcslen(cast(const wchar_t*, str));
#else
	return strlen(cast(const char*, str));
#endif
}


/***********************************************************************
**
*/	REBCHR *OS_STRCHR_(const REBCHR *str, REBCNT ch)
/*
**		Debug-only REBCHR-checked function for OS_STRCHR macro
**
***********************************************************************/
{
#ifdef OS_WIDE_CHAR
	return cast(REBCHR*, wcschr(cast(const wchar_t*, str), ch));
#else
	// We have to m_cast because C++ actually has a separate overload of
	// strchr which will return a const pointer if the in pointer was const
	return cast(REBCHR*, m_cast(char*, strchr(cast(const char*, str), ch)));
#endif
}


/***********************************************************************
**
*/	REBCHR OS_MAKE_CH_(REBCNT ch)
/*
**		Debug-only REBCHR-checked function for OS_MAKE_CH macro
**
***********************************************************************/
{
	REBCHR result;
	result.num = ch;
	return result;
}

#endif
