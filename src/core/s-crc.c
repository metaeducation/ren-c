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

#include "sys-zlib.h" // re-use CRC code from zlib
const z_crc_t *crc32_table; // pointer to the zlib CRC32 table

#define CRCBITS 24 // may be 16, 24, or 32

#define MASK_CRC(crc) \
    ((crc) & INT32_C(0x00ffffff)) // if CRCBITS is 24

#define CRCHIBIT \
    cast(REBCNT, INT32_C(1) << (CRCBITS - 1)) // 0x8000 if CRCBITS is 16

#define CRCSHIFTS (CRCBITS-8)
#define CCITTCRC 0x1021     /* CCITT's 16-bit CRC generator polynomial */
#define PRZCRC   0x864cfb   /* PRZ's 24-bit CRC generator polynomial */
#define CRCINIT  0xB704CE   /* Init value for CRC accumulator */

static REBCNT *crc24_table;

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
static REBCNT Generate_CRC24(REBYTE ch, REBCNT poly, REBCNT accum)
{
    REBINT i;
    REBCNT data;

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
static void Make_CRC24_Table(REBCNT poly)
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
REBINT Compute_CRC24(REBYTE *str, REBCNT len)
{
    REBINT crc = cast(REBINT, len) + cast(REBINT, cast(REBYTE, *str));

    for (; len > 0; len--) {
        REBYTE n = cast(REBYTE, (crc >> CRCSHIFTS) ^ cast(REBYTE, *str++));

        // Left shift math must use unsigned to avoid undefined behavior
        // http://stackoverflow.com/q/3784996/211160
        crc = cast(REBINT, MASK_CRC(cast(REBCNT, crc) << 8) ^ crc24_table[n]);
    }

    return crc;
}


//
//  Hash_UTF8: C
//
// Return a case insensitive hash value for the string.
//
// !!! Review taking size_t for size instead of REBCNT, but Back_Scan_UTF8
// needs to be changed.
//
REBINT Hash_UTF8(const REBYTE *utf8, REBCNT size)
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
        if (n < UNICODE_CASES)
            n = cast(REBYTE, LO_CASE(n));
        else
            n = cast(REBYTE, n);

        n = cast(REBYTE, (hash >> CRCSHIFTS) ^ n);

        // Left shift math must use unsigned to avoid undefined behavior
        // http://stackoverflow.com/q/3784996/211160
        //
        hash = cast(REBINT, MASK_CRC(cast(REBCNT, hash) << 8) ^ crc24_table[n]);
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
    uint32_t hash;

    switch(VAL_TYPE(v)) {
    case REB_MAX_VOID:
        //
        // While a void might technically be hashed, it can't be a value *or*
        // a key in a map.
        //
        panic (NULL);

    case REB_BAR:
    case REB_LIT_BAR:
    case REB_BLANK:
        hash = 0;
        break;

    case REB_LOGIC:
        hash = VAL_LOGIC(v) ? 1 : 0;
        break;

    case REB_INTEGER:
        //
        // R3-Alpha XOR'd with (VAL_INT64(val) >> 32).  But: "XOR with high
        // bits collapses -1 with 0 etc.  (If your key k is |k| < 2^32 high
        // bits are 0-informative." -Giulio
        //
        hash = cast(uint32_t, VAL_INT64(v));
        break;

    case REB_DECIMAL:
    case REB_PERCENT:
        // depends on INT64 sharing the DEC64 bits
        hash = (VAL_INT64(v) >> 32) ^ (VAL_INT64(v));
        break;

    case REB_MONEY: {
        //
        // !!! R3-Alpha used a sketchy "Reb_All" union for this, violating the
        // rule of only reading from the union you last read from.  Access
        // via unsigned char* to use the actual bytes of the money payload to
        // accomplish the same thing (whether it was good or not, at least it
        // isn't breaking the C standard)

        REBYTE *payload = cast(REBYTE*, &v->payload.money);

        uintptr_t bits0;
        uintptr_t bits1;
        memcpy(&bits0, payload, sizeof(uintptr_t));
        memcpy(&bits1, payload + sizeof(uintptr_t), sizeof(uintptr_t));

        hash = bits0 ^ bits1 ^ v->extra.m0;
        break; }

    case REB_CHAR:
        hash = LO_CASE(VAL_CHAR(v));
        break;

    case REB_PAIR: {
        hash = Hash_Value(VAL_PAIR(v));
        hash ^= Hash_Value(PAIRING_KEY(VAL_PAIR(v)));
        break; }

    case REB_TUPLE:
        hash = Hash_Bytes_Or_Uni(VAL_TUPLE(v), VAL_TUPLE_LEN(v), 1);
        break;

    case REB_TIME:
    case REB_DATE:
        hash = cast(REBCNT, VAL_NANO(v) ^ (VAL_NANO(v) / SEC_SEC));
        if (IS_DATE(v))
            hash ^= VAL_DATE(v).bits;
        break;

    case REB_BINARY:
    case REB_TEXT:
    case REB_FILE:
    case REB_EMAIL:
    case REB_URL:
    case REB_TAG:
        hash = Hash_Bytes_Or_Uni(
            VAL_RAW_DATA_AT(v),
            VAL_LEN_HEAD(v),
            SER_WIDE(VAL_SERIES(v))
        );
        break;

    case REB_BLOCK:
    case REB_GROUP:
    case REB_PATH:
    case REB_SET_PATH:
    case REB_GET_PATH:
    case REB_LIT_PATH:
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
        hash = ARR_LEN(VAL_ARRAY(v));
        break;

    case REB_DATATYPE: {
        hash = Hash_String(Canon(VAL_TYPE_SYM(v)));
        break; }

    case REB_BITSET:
    case REB_IMAGE:
    case REB_VECTOR:
    case REB_TYPESET:
        //
        // These types are currently not supported.
        //
        // !!! Why not?
        //
        fail (Error_Invalid_Type(VAL_TYPE(v)));

    case REB_WORD:
    case REB_SET_WORD:
    case REB_GET_WORD:
    case REB_LIT_WORD:
    case REB_REFINEMENT:
    case REB_ISSUE: {
        //
        // Note that the canon symbol may change for a group of word synonyms
        // if that canon is GC'd--it picks another synonym.  Thus the pointer
        // of the canon cannot be used as a long term hash.  A case insensitive
        // hashing of the word spelling itself is needed.
        //
        // !!! Should this hash be cached on the words somehow, e.g. in the
        // data payload before the actual string?
        //
        hash = Hash_String(VAL_WORD_SPELLING(v));
        break; }

    case REB_ACTION:
        //
        // Because function equality is by identity only and they are
        // immutable once created, it is legal to put them in hashes.  The
        // VAL_ACT is the paramlist series, guaranteed unique per function
        //
        hash = cast(REBCNT, cast(uintptr_t, VAL_ACTION(v)) >> 4);
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
        hash = cast(uint32_t, cast(uintptr_t, VAL_CONTEXT(v)) >> 4);
        break;

    case REB_MAP:
        //
        // Looking up a map in a map is fairly analogous to looking up an
        // object in a map.  If one is permitted, so should the other be.
        // (Again this will just find the map by identity, not by comparing
        // the values of one against the values of the other...)
        //
        hash = cast(uint32_t, cast(uintptr_t, VAL_MAP(v)) >> 4);
        break;

    case REB_GOB:
    case REB_EVENT:
    case REB_HANDLE:
    case REB_STRUCT:
    case REB_LIBRARY:
        //
        // !!! Review hashing behavior or needs of these types if necessary.
        //
        fail (Error_Invalid_Type(VAL_TYPE(v)));

    default:
        // The list above should be comprehensive.  panic in order to keep
        // there from being an uninitialized ret warning.
        //
        panic (NULL);
    }

    return hash ^ crc32_table[VAL_TYPE(v)];
}


//
//  Make_Hash_Sequence: C
//
REBSER *Make_Hash_Sequence(REBCNT len)
{
    REBCNT n = Get_Hash_Prime(len * 2); // best when 2X # of keys
    if (n == 0) {
        DECLARE_LOCAL (temp);
        Init_Integer(temp, len);

        fail (Error_Size_Limit_Raw(temp));
    }

    REBSER *ser = Make_Series(n + 1, sizeof(REBCNT));
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
        ENSURE_SERIES_MANAGED(MAP_HASHLIST(map));

    ENSURE_ARRAY_MANAGED(MAP_PAIRLIST(map));

    RESET_VAL_HEADER(out, REB_MAP);
    INIT_BINDING(out, UNBOUND);
    out->payload.any_series.series = SER(MAP_PAIRLIST(map));
    out->payload.any_series.index = 0;

    return KNOWN(out);
}


//
//  Hash_Block: C
//
// Hash ALL values of a block. Return hash array series.
// Used for SET logic (unique, union, etc.)
//
// Note: hash array contents (indexes) are 1-based!
//
REBSER *Hash_Block(const REBVAL *block, REBCNT skip, REBOOL cased)
{
    REBCNT n;
    REBSER *hashlist;
    REBCNT *hashes;
    REBARR *array = VAL_ARRAY(block);
    RELVAL *value;

    // Create the hash array (integer indexes):
    hashlist = Make_Hash_Sequence(VAL_LEN_AT(block));
    hashes = SER_HEAD(REBCNT, hashlist);

    value = VAL_ARRAY_AT(block);
    if (IS_END(value))
        return hashlist;

    n = VAL_INDEX(block);
    while (TRUE) {
        REBCNT skip_index = skip;

        REBCNT hash = Find_Key_Hashed(
            array, hashlist, value, VAL_SPECIFIER(block), 1, cased, 0
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
REBINT Compute_IPC(REBYTE *data, REBCNT length)
{
    REBCNT  lSum = 0;   // stores the summation
    REBYTE  *up = data;

    while (length > 1) {
        lSum += (up[0] << 8) | up[1];
        up += 2;
        length -= 2;
    }

    // Handle the odd byte if necessary
    if (length) lSum += *up;

    // Add back the carry outs from the 16 bits to the low 16 bits
    lSum = (lSum >> 16) + (lSum & 0xffff);  // Add high-16 to low-16
    lSum += (lSum >> 16);                   // Add carry
    return (REBINT)( (~lSum) & 0xffff);     // 1's complement, then truncate
}



//
//  Hash_Bytes_Or_Uni: C
//
// Return a 32-bit case insensitive hash value for the string.  The
// string does not have to be zero terminated and UTF8 is ok.
//
REBINT Hash_Bytes_Or_Uni(
    const void *data, // REBYTE* or REBUNI*
    REBCNT len, // chars, not bytes
    REBCNT wide // 1 = byte-sized, 2 = Unicode
){
    uint32_t c = 0x00000000;
    uint32_t c2 = 0x00000000; // don't change, see [1] below
    REBCNT n;
    const REBYTE *b = cast(const REBYTE*, data);
    const REBUNI *u = cast(const REBUNI*, data);

    if (wide == 1) {
        for(n = 0; n != len; n++) {
            c = (c >> 8) ^ crc32_table[(c ^ LO_CASE(b[n])) & 0xff];
        }
    } else if (wide == 2) {
        for(n = 0; n != len; n++) {
            c = (c >> 8) ^ crc32_table[(c ^ LO_CASE(u[n])) & 0xff];

            c2 = (c2 >> 8) ^ crc32_table[
                (c2 ^ (LO_CASE(u[n]) >> 8)) & 0xff
            ];
        }
    }
    else
        assert(wide == 1 || wide == 2);

    // [1] If wide = 2 but all chars <= 0xFF then c2 = 0, and c is the same
    // as wide = 1
    //
    c ^= c2;

    return cast(REBINT,~c);
}


//
//  Startup_CRC: C
//
void Startup_CRC(void)
{
    crc24_table = ALLOC_N(REBCNT, 256);
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

    FREE_N(REBCNT, 256, crc24_table);
}
