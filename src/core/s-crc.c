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
**  Module:  s-crc.c
**  Summary: CRC computation
**  Section: strings
**  Author:  Carl Sassenrath (REBOL interface sections)
**  Notes:
**
***********************************************************************/

#include "sys-core.h"

#define CRC_DEFINED

#define CRCBITS 24          /* may be 16, 24, or 32 */
#define MASK_CRC(crc) ((crc) & I32_C(0x00ffffff))     /* if CRCBITS is 24 */
#define CRCHIBIT ((REBCNT) (I32_C(1)<<(CRCBITS-1))) /* 0x8000 if CRCBITS is 16 */
#define CRCSHIFTS (CRCBITS-8)
#define CCITTCRC 0x1021     /* CCITT's 16-bit CRC generator polynomial */
#define PRZCRC   0x864cfb   /* PRZ's 24-bit CRC generator polynomial */
#define CRCINIT  0xB704CE   /* Init value for CRC accumulator */

static REBCNT *CRC_Table;

//
//  Generate_CRC: C
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
static REBCNT Generate_CRC(REBYTE ch, REBCNT poly, REBCNT accum)
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
//  Make_CRC_Table: C
// 
// Derives a CRC lookup table from the CRC polynomial.
// The table is used later by crcupdate function given below.
// Only needs to be called once at the dawn of time.
//
static void Make_CRC_Table(REBCNT poly)
{
    REBINT i;

    for (i = 0; i < 256; i++)
        CRC_Table[i] = Generate_CRC(cast(REBYTE, i), poly, 0);
}


//
//  Compute_CRC: C
// 
// Rebol had canonized signed numbers for CRCs, and the signed logic
// actually does turn high bytes into negative numbers so they
// subtract instead of add *during* the calculation.  Hence the casts
// are necessary so long as compatibility with the historical results
// of the CHECKSUM native is needed.
//
REBINT Compute_CRC(REBYTE *str, REBCNT len)
{
    REBINT crc = cast(REBINT, len) + cast(REBINT, cast(REBYTE, *str));

    for (; len > 0; len--) {
        REBYTE n = cast(REBYTE, (crc >> CRCSHIFTS) ^ cast(REBYTE, *str++));

        // Left shift math must use unsigned to avoid undefined behavior
        // http://stackoverflow.com/q/3784996/211160
        crc = cast(REBINT, MASK_CRC(cast(REBCNT, crc) << 8) ^ CRC_Table[n]);
    }

    return crc;
}


//
//  Hash_Word: C
// 
// Return a case insensitive hash value for the string.
//
REBINT Hash_Word(const REBYTE *str, REBCNT len)
{
    REBINT hash =
        cast(REBINT, len) + cast(REBINT, cast(REBYTE, LO_CASE(*str)));

    for (; len > 0; str++, len--) {
        REBUNI n = *str;

        if (n >= 0x80) {
            str = Back_Scan_UTF8_Char(&n, str, &len);
            assert(str); // UTF8 should have already been verified good
        }

        // Optimize `n = cast(REBYTE, LO_CASE(n))` (drop upper 8 bits)
        // !!! Is this actually faster?
        if (n < UNICODE_CASES)
            n = cast(REBYTE, LO_CASE(n));
        else
            n = cast(REBYTE, n);

        n = cast(REBYTE, (hash >> CRCSHIFTS) ^ n);

        // Left shift math must use unsigned to avoid undefined behavior
        // http://stackoverflow.com/q/3784996/211160
        hash = cast(REBINT, MASK_CRC(cast(REBCNT, hash) << 8) ^ CRC_Table[n]);
    }

    return hash;
}

static u32 *crc32_table = 0;

static void Make_CRC32_Table(void);


//
//  Hash_Value: C
// 
// Return a case insensitive hash value for any value.
// 
// Fails if datatype cannot be hashed.
//
REBCNT Hash_Value(const REBVAL *val)
{
    REBCNT ret;
    const REBYTE *name;

    switch(VAL_TYPE(val)) {

    case REB_BAR:
    case REB_LIT_BAR:
    case REB_NONE:
    case REB_UNSET:
        ret = 0;
        break;

    case REB_LOGIC:
        ret = VAL_LOGIC(val) ? 1 : 0;
        break;

    case REB_INTEGER:
        //
        // R3-Alpha XOR'd with (VAL_INT64(val) >> 32).  But: "XOR with high
        // bits collapses -1 with 0 etc.  (If your key k is |k| < 2^32 high
        // bits are 0-informative." -Giulio
        //
        ret = cast(REBCNT, VAL_INT64(val));
        break;

    case REB_DECIMAL:
    case REB_PERCENT:
        // depends on INT64 sharing the DEC64 bits
        ret = (VAL_INT64(val) >> 32) ^ (VAL_INT64(val));
        break;

    case REB_MONEY:
        ret = VAL_ALL_BITS(val)[0] ^ VAL_ALL_BITS(val)[1] ^ VAL_ALL_BITS(val)[2];
        break;

    case REB_CHAR:
        ret = LO_CASE(VAL_CHAR(val));
        break;

    case REB_PAIR:
        ret = (VAL_ALL_BITS(val)[0] << 16) ^ (VAL_ALL_BITS(val)[0] >> 16) ^ (VAL_ALL_BITS(val)[1]);
        break;

    case REB_TUPLE:
        ret = Hash_String(VAL_TUPLE(val), VAL_TUPLE_LEN(val), 1);
        break;

    case REB_TIME:
    case REB_DATE:
        ret = (REBCNT)(VAL_TIME(val) ^ (VAL_TIME(val) / SEC_SEC));
        if (IS_DATE(val)) ret ^= VAL_DATE(val).bits;
        break;

    case REB_BINARY:
    case REB_STRING:
    case REB_FILE:
    case REB_EMAIL:
    case REB_URL:
    case REB_TAG:
        ret = Hash_String(VAL_RAW_DATA_AT(val), VAL_LEN_HEAD(val), SERIES_WIDE(VAL_SERIES(val)));
        break;

    case REB_BLOCK:
    case REB_GROUP:
    case REB_PATH:
    case REB_SET_PATH:
    case REB_GET_PATH:
    case REB_LIT_PATH:
        //
        // Using an array in a map if it is mutable, and then comparing by
        // value (vs. comparing identity with SAME?), would require making
        // a deep copy of that array.  This has been considered too expensive.
        //
        // !!! There could be ways to make this work...such as allowing
        // a PROTECT/DEEP array to be locked and stay locked as the key...
        // and then have a lightweight hash of it.  Review if needed.
        //
        fail (Error_Has_Bad_Type(val));

    case REB_DATATYPE:
        name = Get_Sym_Name(VAL_TYPE_SYM(val));
        ret = Hash_Word(name, LEN_BYTES(name));
        break;

    case REB_BITSET:
    case REB_IMAGE:
    case REB_VECTOR:
    case REB_TYPESET:
        //
        // These types are currently not supported.
        //
        // !!! Why not?
        //
        fail (Error_Has_Bad_Type(val));

    case REB_WORD:
    case REB_SET_WORD:
    case REB_GET_WORD:
    case REB_LIT_WORD:
    case REB_REFINEMENT:
    case REB_ISSUE:
        ret = VAL_WORD_CANON(val);
        break;

    case REB_NATIVE:
    case REB_ACTION:
    case REB_ROUTINE:
    case REB_COMMAND:
    case REB_CLOSURE:
    case REB_FUNCTION:
        //
        // ANY-FUNCTION has a uniquely identifying "func" pointer for that
        // function.  Because function equality is by identity only and they
        // are immutable once created, it is legal to put them in hashes.
        //
        ret = cast(REBCNT, cast(REBUPT, VAL_FUNC(val)) >> 4);
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
        ret = cast(REBCNT, cast(REBUPT, VAL_CONTEXT(val)) >> 4);
        break;

    case REB_MAP:
        //
        // Looking up a map in a map is fairly analogous to looking up an
        // object in a map.  If one is permitted, so should the other be.
        // (Again this will just find the map by identity, not by comparing
        // the values of one against the values of the other...)
        //
        ret = cast(REBCNT, cast(REBUPT, VAL_MAP(val)) >> 4);
        break;

    case REB_TASK:
    case REB_GOB:
    case REB_EVENT:
    case REB_CALLBACK:
    case REB_HANDLE:
    case REB_STRUCT:
    case REB_LIBRARY:
        //
        // !!! Review hashing behavior or needs of these types if necessary.
        //
        fail (Error_Has_Bad_Type(val));

    default:
        assert(FALSE); // the list above should be comprehensive
    }

    if(!crc32_table) Make_CRC32_Table();

    return ret ^ crc32_table[VAL_TYPE(val)];
}


//
//  Make_Hash_Sequence: C
//
REBSER *Make_Hash_Sequence(REBCNT len)
{
    REBCNT n;
    REBSER *ser;

    n = Get_Hash_Prime(len * 2); // best when 2X # of keys
    if (!n) {
        REBVAL temp;
        VAL_INIT_WRITABLE_DEBUG(&temp);
        SET_INTEGER(&temp, len);
        fail (Error(RE_SIZE_LIMIT, &temp));
    }

    ser = Make_Series(n + 1, sizeof(REBCNT), MKS_NONE);
    Clear_Series(ser);
    SET_SERIES_LEN(ser, n);

    return ser;
}


//
//  Val_Init_Map: C
// 
// A map has an additional hash element hidden in the ->extra
// field of the REBSER which needs to be given to memory
// management as well.
//
void Val_Init_Map(REBVAL *out, REBMAP *map)
{
    if (MAP_HASHLIST(map))
        ENSURE_SERIES_MANAGED(MAP_HASHLIST(map));

    Val_Init_Array(out, REB_MAP, MAP_PAIRLIST(map));
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
    REBVAL *value;

    // Create the hash array (integer indexes):
    hashlist = Make_Hash_Sequence(VAL_LEN_AT(block));
    hashes = SERIES_HEAD(REBCNT, hashlist);

    value = VAL_ARRAY_AT(block);
    if (IS_END(value))
        return hashlist;

    n = VAL_INDEX(block);
    while (TRUE) {
        REBCNT skip_index = skip;

        REBCNT hash = Find_Key_Hashed(array, hashlist, value, 1, cased, 0);
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
                    fail (Error(RE_BLOCK_SKIP_WRONG));
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


static void Make_CRC32_Table(void) {
    u32 c;
    int n,k;

    crc32_table = ALLOC_N(u32, 256);

    for(n=0;n<256;n++) {
        c=(u32)n;
        for(k=0;k<8;k++) {
            if(c&1)
                c=U32_C(0xedb88320)^(c>>1);
            else
                c=c>>1;
        }
        crc32_table[n]=c;
    }
}


REBCNT Update_CRC32(u32 crc, REBYTE *buf, int len) {
    u32 c = ~crc;
    int n;

    if(!crc32_table) Make_CRC32_Table();

    for(n = 0; n < len; n++)
        c = crc32_table[(c^buf[n])&0xff]^(c>>8);

    return ~c;
}


//
//  CRC32: C
//
REBCNT CRC32(REBYTE *buf, REBCNT len)
{
    return Update_CRC32(U32_C(0x00000000), buf, len);
}


//
//  Hash_String: C
// 
// Return a 32-bit case insensitive hash value for the string.  The
// string does not have to be zero terminated and UTF8 is ok.
//
REBINT Hash_String(
        const void *data, // REBYTE* or REBUNI*
        REBCNT len, // chars, not bytes
        REBCNT wide // 1 = byte-sized, 2 = Unicode
) {
    u32 c = 0x00000000;
    u32 c2 = 0x00000000; // don't change, see [1] below
    REBCNT n;
    const REBYTE *b = cast(REBYTE*, data);
    const REBUNI *u = cast(REBUNI*, data);

    if(!crc32_table) Make_CRC32_Table();

    if (wide == 1) {
        for(n = 0; n < len; n++) {
            c = (c >> 8) ^ crc32_table[(c ^ LO_CASE(b[n])) & 0xff];
        }
    } else if (wide == 2) {
        for(n = 0; n < len; n++) {
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
//  Init_CRC: C
//
void Init_CRC(void)
{
    CRC_Table = ALLOC_N(REBCNT, 256);
    Make_CRC_Table(PRZCRC);
}


//
//  Shutdown_CRC: C
//
void Shutdown_CRC(void)
{
    if (crc32_table) FREE_N(u32, 256, crc32_table);

    FREE_N(REBCNT, 256, CRC_Table);
}
