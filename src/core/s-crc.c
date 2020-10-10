//
//  File: %s-crc.c
//  Summary: "CRC computation"
//  Section: strings
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

#include "datatypes/sys-money.h" // !!! Needed for hash (should be a method?)

#include "sys-zlib.h" // re-use CRC code from zlib
const z_crc_t *crc32_table; // pointer to the zlib CRC32 table

#define CRCBITS 24 // may be 16, 24, or 32

#define MASK_CRC(crc) \
    ((crc) & INT32_C(0x00ffffff)) // if CRCBITS is 24

#define CRCHIBIT \
    cast(REBLEN, INT32_C(1) << (CRCBITS - 1)) // 0x8000 if CRCBITS is 16

#define CRCSHIFTS (CRCBITS-8)
#define CCITTCRC 0x1021     /* CCITT's 16-bit CRC generator polynomial */
#define PRZCRC   0x864cfb   /* PRZ's 24-bit CRC generator polynomial */
#define CRCINIT  0xB704CE   /* Init value for CRC accumulator */

static REBLEN *crc24_table;

//
//  Generate_CRC24: C
//
// Simulates CRC hardware circuit.  Generates true CRC
// directly, without requiring extra NULL bytes to be appended
// to the message. Returns new updated CRC accumulator.
//
// These CRC functions are derived from code in chapter 19 of the book
// "C Programmer's Guide to Serial Communications", by Joe Campbell.
// Generalized to any CRC width by Philip Zimmermann.
//
//     CRC-16        X^16 + X^15 + X^2 + 1
//     CRC-CCITT    X^16 + X^12 + X^2 + 1
//
// Notes on making a good 24-bit CRC:
// The primitive irreducible polynomial of degree 23 over GF(2),
// 040435651 (octal), comes from Appendix C of "Error Correcting Codes,
// 2nd edition" by Peterson and Weldon, page 490.  This polynomial was
// chosen for its uniform density of ones and zeros, which has better
// error detection properties than polynomials with a minimal number of
// nonzero terms.    Multiplying this primitive degree-23 polynomial by
// the polynomial x+1 yields the additional property of detecting any
// odd number of bits in error, which means it adds parity.  This
// approach was recommended by Neal Glover.
//
// To multiply the polynomial 040435651 by x+1, shift it left 1 bit and
// bitwise add (xor) the unshifted version back in.  Dropping the unused
// upper bit (bit 24) produces a CRC-24 generator bitmask of 041446373
// octal, or 0x864cfb hex.
//
// You can detect spurious leading zeros or framing errors in the
// message by initializing the CRC accumulator to some agreed-upon
// nonzero "random-like" value, but this is a bit nonstandard.
//
static REBLEN Generate_CRC24(REBYTE ch, REBLEN poly, REBLEN accum)
{
    REBINT i;
    REBLEN data;

    data = ch;
    data <<= CRCSHIFTS;     /* shift data to line up with MSB of accum */
    i = 8;                  /* counts 8 bits of data */
    do {    /* if MSB of (data XOR accum) is TRUE, shift and subtract poly */
        if ((data ^ accum) & CRCHIBIT) accum = (accum<<1) ^ poly;
        else accum <<= 1;
        data <<= 1;
    } while (--i);  /* counts 8 bits of data */
    return (MASK_CRC(accum));
}


//
//  Make_CRC24_Table: C
//
// Derives a CRC lookup table from the CRC polynomial.
// The table is used later by crcupdate function given below.
// Only needs to be called once at the dawn of time.
//
static void Make_CRC24_Table(REBLEN poly)
{
    REBINT i;

    for (i = 0; i < 256; i++)
        crc24_table[i] = Generate_CRC24(cast(REBYTE, i), poly, 0);
}


//
//  Compute_CRC24: C
//
// Rebol had canonized signed numbers for CRCs, and the signed logic
// actually does turn high bytes into negative numbers so they
// subtract instead of add *during* the calculation.  Hence the casts
// are necessary so long as compatibility with the historical results
// of the CHECKSUM native is needed.
//
REBINT Compute_CRC24(const REBYTE *str, REBLEN len)
{
    REBINT crc = cast(REBINT, len) + cast(REBINT, cast(REBYTE, *str));

    for (; len > 0; len--) {
        REBYTE n = cast(REBYTE, (crc >> CRCSHIFTS) ^ cast(REBYTE, *str++));

        // Left shift math must use unsigned to avoid undefined behavior
        // http://stackoverflow.com/q/3784996/211160
        crc = cast(REBINT, MASK_CRC(cast(REBLEN, crc) << 8) ^ crc24_table[n]);
    }

    return crc;
}


//
//  Hash_UTF8: C
//
// Return a case insensitive hash value for the string.
//
REBINT Hash_UTF8(const REBYTE *utf8, REBSIZ size)
{
    REBINT hash =
        cast(REBINT, size) + cast(REBINT, cast(REBYTE, LO_CASE(*utf8)));

    for (; size != 0; ++utf8, --size) {
        REBUNI n = *utf8;

        if (n >= 0x80) {
            utf8 = Back_Scan_UTF8_Char(&n, utf8, &size);
            assert(utf8 != NULL); // should have already been verified good
        }

        // Optimize `n = cast(REBYTE, LO_CASE(n))` (drop upper 8 bits)
        // !!! Is this actually faster?
        //
        n = cast(REBYTE, LO_CASE(n));

        n = cast(REBYTE, (hash >> CRCSHIFTS) ^ n);

        // Left shift math must use unsigned to avoid undefined behavior
        // http://stackoverflow.com/q/3784996/211160
        //
        hash = cast(REBINT, MASK_CRC(cast(REBLEN, hash) << 8) ^ crc24_table[n]);
    }

    return hash;
}



//
//  Hash_Value: C
//
// Return a case insensitive hash value for any value.
//
// Fails if datatype cannot be hashed.  Note that the specifier is not used
// in hashing, because it is not used in comparisons either.
//
uint32_t Hash_Value(const RELVAL *v)
{
    REBCEL(const*) cell = VAL_UNESCAPED(v); // hash contained quoted content
    enum Reb_Kind type = CELL_TYPE(cell);

    uint32_t hash;

    switch (type) {
      case REB_NULL:
        panic ("Cannot hash NULL");  // nulls can't be values or keys in MAP!s

      case REB_VOID:
      case REB_BLANK:
        hash = 0;
        break;

      case REB_LOGIC:
        hash = VAL_LOGIC(cell) ? 1 : 0;
        break;

      case REB_INTEGER:
        //
        // R3-Alpha XOR'd with (VAL_INT64(val) >> 32).  But: "XOR with high
        // bits collapses -1 with 0 etc.  (If your key k is |k| < 2^32 high
        // bits are 0-informative." -Giulio
        //
        hash = cast(uint32_t, VAL_INT64(cell));
        break;

      case REB_DECIMAL:
      case REB_PERCENT:
        // depends on INT64 sharing the DEC64 bits
        hash = (VAL_INT64(cell) >> 32) ^ (VAL_INT64(cell));
        break;

      case REB_MONEY: {
        //
        // Writes the 3 pointer fields as three uintptr_t integer values to
        // build a `deci` type.  So it is safe to read the three pointers as
        // uintptr_t back, and hash them.
        //
        hash = PAYLOAD(Any, cell).first.u;
        hash ^= PAYLOAD(Any, cell).second.u;
        hash ^= EXTRA(Any, cell).u;
        break; }

      case REB_PAIR:
        hash = Hash_Value(VAL_PAIR_X(cell));
        hash ^= Hash_Value(VAL_PAIR_Y(cell));
        break;

      case REB_TIME:
      case REB_DATE:
        hash = cast(REBLEN, VAL_NANO(cell) ^ (VAL_NANO(cell) / SEC_SEC));
        if (type == REB_DATE) {
            //
            // !!! This hash used to be done with an illegal-in-C union alias
            // of bit fields.  This shift is done to account for the number
            // of bits in each field, giving a compatible effect.
            //
            REBYMD d = VAL_DATE(cell);
            hash ^= (
                ((((((d.year << 16) + d.month) << 4) + d.day) << 5)
                    + d.zone) << 7
            );
        }
        break;

      case REB_BINARY:
        hash = Hash_Bytes(VAL_BIN_AT(cell), VAL_LEN_AT(cell));
        break;

      case REB_TEXT:
      case REB_FILE:
      case REB_EMAIL:
      case REB_URL:
      case REB_TAG:
      case REB_ISSUE: {  // ISSUE! heart may be REB_BYTES, VAL_UTF8_X handles
        REBSIZ size;
        REBCHR(const*) utf8 = VAL_UTF8_SIZE_AT(&size, cell);
        hash = Hash_UTF8_Caseless(utf8, size);
        break; }

      case REB_TUPLE:
      case REB_SET_TUPLE:
      case REB_GET_TUPLE:
      case REB_SYM_TUPLE:
        //
      case REB_PATH:
      case REB_SET_PATH:
      case REB_GET_PATH:
      case REB_SYM_PATH: {
        enum Reb_Kind heart = CELL_KIND(cell);  // use correct hash for heart
        switch (heart) {
          case REB_BYTES:
            hash = Hash_Bytes(
                PAYLOAD(Bytes, cell).at_least_8,
                EXTRA(Bytes, cell).exactly_4[IDX_EXTRA_USED]
            );
            break;

          case REB_WORD:
          case REB_GET_WORD:
          case REB_SYM_WORD:
            goto hash_any_word;

          case REB_BLOCK:
            goto hash_any_array;

          default:
            panic (nullptr);
        }
        break; }
 
      hash_any_array:
        //
      case REB_GROUP:
      case REB_SET_GROUP:
      case REB_GET_GROUP:
      case REB_SYM_GROUP:
        //
      case REB_BLOCK:
      case REB_SET_BLOCK:
      case REB_GET_BLOCK:
      case REB_SYM_BLOCK:
        //
        // !!! Lame hash just to get it working.  There will be lots of
        // collisions.  Intentionally bad to avoid writing something that
        // is less obviously not thought out.
        //
        // Whatever hash is used must be able to match lax equality.  So it
        // could hash all the values case-insensitively, or the first N values,
        // or something.
        //
        // Note that if there is a way to mutate this array, there will be
        // problems.  Do not hash mutable arrays unless you are sure hashings
        // won't cross a mutation.
        //
        hash = ARR_LEN(VAL_ARRAY(cell));
        break;

      case REB_DATATYPE: {
        hash = Hash_String(Canon(SYM_FROM_KIND(VAL_TYPE_KIND(cell))));
        break; }

      case REB_BITSET:
      case REB_TYPESET:
        //
        // "These types are currently not supported."
        //
        // !!! Why not?
        //
        fail (Error_Invalid_Type(type));

      hash_any_word:
        //
      case REB_WORD:
      case REB_SET_WORD:
      case REB_GET_WORD:
      case REB_SYM_WORD: {
        //
        // Note that the canon symbol may change for a group of word synonyms
        // if that canon is GC'd--it picks another synonym.  Thus the pointer
        // of the canon cannot be used as a long term hash.  A case insensitive
        // hashing of the word spelling itself is needed.
        //
        // !!! Should this hash be cached on the words somehow, e.g. in the
        // data payload before the actual string?
        //
        hash = Hash_String(VAL_WORD_SPELLING(cell));
        break; }

      case REB_ACTION:
        //
        // Because function equality is by identity only and they are
        // immutable once created, it is legal to put them in hashes.  The
        // VAL_ACT is the paramlist series, guaranteed unique per function
        //
        hash = cast(REBLEN, cast(uintptr_t, VAL_ACTION(cell)) >> 4);
        break;

      case REB_FRAME:
      case REB_MODULE:
      case REB_ERROR:
      case REB_PORT:
      case REB_OBJECT:
        //
        // !!! ANY-CONTEXT has a uniquely identifying context pointer for that
        // context.  However, this does not help with "natural =" comparison
        // as the hashing will be for SAME? contexts only:
        //
        // http://stackoverflow.com/a/33577210/211160
        //
        // Allowing object keys to be OBJECT! and then comparing by field
        // values creates problems for hashing if that object is mutable.
        // However, since it was historically allowed it is allowed for
        // all ANY-CONTEXT! types at the moment.
        //
        hash = cast(uint32_t, cast(uintptr_t, VAL_CONTEXT(cell)) >> 4);
        break;

      case REB_MAP:
        //
        // Looking up a map in a map is fairly analogous to looking up an
        // object in a map.  If one is permitted, so should the other be.
        // (Again this will just find the map by identity, not by comparing
        // the values of one against the values of the other...)
        //
        hash = cast(uint32_t, cast(uintptr_t, VAL_MAP(cell)) >> 4);
        break;

      case REB_EVENT:
      case REB_HANDLE:
        //
        // !!! Review hashing behavior or needs of these types if necessary.
        //
        fail (Error_Invalid_Type(type));

      case REB_CUSTOM:
        //
        // !!! We don't really know how to hash a custom value.  Knowing what
        // the answer is ties into the equality operator.  It should be one
        // of the extensibility hooks.
        //
        fail (Error_Invalid_Type(type));

      default:
        panic (nullptr); // List should be comprehensive
    }

    return hash ^ crc32_table[type];
}


//
//  Make_Hash_Series: C
//
REBSER *Make_Hash_Series(REBLEN len)
{
    REBLEN n = Get_Hash_Prime_May_Fail(len * 2);  // best when 2X # of keys
    REBSER *ser = Make_Series(n + 1, sizeof(REBLEN));
    Clear_Series(ser);
    SET_SERIES_LEN(ser, n);

    return ser;
}


//
//  Init_Map: C
//
// A map has an additional hash element hidden in the ->extra
// field of the REBSER which needs to be given to memory
// management as well.
//
REBVAL *Init_Map(RELVAL *out, REBMAP *map)
{
    if (MAP_HASHLIST(map))
        Force_Series_Managed(MAP_HASHLIST(map));

    Force_Array_Managed(MAP_PAIRLIST(map));

    RESET_CELL(out, REB_MAP, CELL_FLAG_FIRST_IS_NODE);
    INIT_VAL_NODE(out, MAP_PAIRLIST(map));
    // second payload pointer not used

    return SPECIFIC(out);
}


//
//  Hash_Block: C
//
// Hash ALL values of a block. Return hash array series.
// Used for SET logic (unique, union, etc.)
//
// Note: hash array contents (indexes) are 1-based!
//
REBSER *Hash_Block(const REBVAL *block, REBLEN skip, bool cased)
{
    // Create the hash array (integer indexes):
    REBSER *hashlist = Make_Hash_Series(VAL_LEN_AT(block));

    const RELVAL *value = VAL_ARRAY_AT(block);
    if (IS_END(value))
        return hashlist;

    REBLEN *hashes = SER_HEAD(REBLEN, hashlist);

    const REBARR *array = VAL_ARRAY(block);
    REBLEN n = VAL_INDEX(block);

    while (true) {
        REBLEN skip_index = skip;

        REBLEN hash = Find_Key_Hashed(
            m_cast(REBARR*, array),  // mode == 0, no modification, cast ok
            hashlist,
            value,
            VAL_SPECIFIER(block),
            1,
            cased,
            0  // mode
        );
        hashes[hash] = (n / skip) + 1;

        while (skip_index != 0) {
            value++;
            n++;
            skip_index--;

            if (IS_END(value)) {
                if (skip_index != 0) {
                    //
                    // !!! It's not clear what to do when hashing something
                    // for a skip index when the number isn't evenly divisible
                    // by that amount.  It means a hash lookup will find
                    // something, but it won't be a "full record".  Just as
                    // we have to check for ENDs inside the hashed-to material
                    // here, later code would have to check also.
                    //
                    // The conservative thing to do here is to error.  If a
                    // compelling coherent behavior and rationale in the
                    // rest of the code can be established.  But more likely
                    // than not, this will catch bugs in callers vs. be
                    // a roadblock to them.
                    //
                    fail (Error_Block_Skip_Wrong_Raw());
                }

                return hashlist;
            }
        }
    }

    DEAD_END;
}


//
//  Compute_IPC: C
//
// Compute an IP checksum given some data and a length.
// Used only on BINARY values.
//
REBINT Compute_IPC(const REBYTE *data, REBSIZ size)
{
    REBLEN sum = 0;  // stores the summation
    const REBYTE *bp = data;

    while (size > 1) {
        sum += (bp[0] << 8) | bp[1];
        bp += 2;
        size -= 2;
    }

    if (size != 0)
        sum += *bp;  // Handle the odd byte if necessary

    // Add back the carry outs from the 16 bits to the low 16 bits

    sum = (sum >> 16) + (sum & 0xffff);  // Add high-16 to low-16
    sum += (sum >> 16);  // Add carry
    return cast(REBINT, (~sum) & 0xffff);  // 1's complement, then truncate
}



//
//  Hash_Bytes: C
//
// Return a 32-bit hash value for the bytes.
//
REBINT Hash_Bytes(const REBYTE *data, REBLEN len) {
    uint32_t crc = 0x00000000;

    REBLEN n;
    for (n = 0; n != len; ++n)
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[n]) & 0xff];

    return cast(REBINT, ~crc);
}


//
//  Hash_UTF8_Caseless: C
//
// Return a 32-bit case insensitive hash value for UTF-8 data.  Length is in
// characters, not bytes.
//
// !!! See redundant code in Hash_UTF8 which takes a size, not a length
//
REBINT Hash_UTF8_Caseless(const REBYTE *utf8, REBLEN len) {
    //
    // Note: can't make the argument a REBCHR() because the C++ build and C
    // build can't have different ABIs for %sys-core.h
    //
    REBCHR(const*) cp = cast(REBCHR(const*), utf8);

    uint32_t crc = 0x00000000;

    REBLEN n;
    for (n = 0; n < len; n++) {
        REBUNI c;
        cp = NEXT_CHR(&c, cp);

        c = LO_CASE(c);

        // !!! This takes into account all 4 bytes of the lowercase codepoint
        // for the CRC calculation.  In ASCII strings this will involve a lot
        // of zeros.  Review if there's a better way.
        //
        crc = (crc >> 8) ^ crc32_table[(crc ^ c) & 0xff];
        crc = (crc >> 8) ^ crc32_table[(crc ^ (c >> 8)) & 0xff];
        crc = (crc >> 8) ^ crc32_table[(crc ^ (c >> 16)) & 0xff];
        crc = (crc >> 8) ^ crc32_table[(crc ^ (c >> 24)) & 0xff];
    }

    return cast(REBINT, ~crc);
}


//
//  Startup_CRC: C
//
void Startup_CRC(void)
{
    crc24_table = TRY_ALLOC_N(REBLEN, 256);
    Make_CRC24_Table(PRZCRC);

    // If Zlib is built with DYNAMIC_CRC_TABLE, then the first call to
    // get_crc_table() will initialize crc_table (for CRC32).  Otherwise the
    // table is precompiled-in.
    //
    crc32_table = get_crc_table();
}


//
//  Shutdown_CRC: C
//
void Shutdown_CRC(void)
{
    // Zlib's DYNAMIC_CRC_TABLE uses a global array, that is not malloc()'d,
    // so nothing to free.

    FREE_N(REBLEN, 256, crc24_table);
}
