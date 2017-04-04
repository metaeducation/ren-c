//
//  File: %u-compress.c
//  Summary: "interface to zlib compression"
//  Section: utility
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
// The Rebol executable includes a version of zlib which has been extracted
// from the GitHub archive and pared down into a single .h and .c file.
// This wraps that functionality into functions that compress and decompress
// BINARY! REBSERs.
//
// Classically, Rebol added a 32-bit size header onto the front of compressed
// data, indicating the uncompressed size.  This is the default BINARY! format
// returned by COMPRESS.  However, it only used a 32-bit number...gzip also
// includes the length modulo 32.  This means that if the data is < 4MB in
// size you can use the length with gzip:
//
// http://stackoverflow.com/a/9213826/211160
//
// Options are offered for using zlib envelope, gzip envelope, or raw deflate.
//
// !!! Technically zlib is designed to do "streaming" compression.  Those
// features are not exposed by this interface, although they are implemented
// in the zlib code.
//

#include "sys-core.h"
#include "sys-zlib.h"


//
//  REBCNT_To_Bytes: C
//
// Get endian-independent encoding of a 32-bit unsigned integer to 4 bytes
//
static void REBCNT_To_Bytes(REBYTE *out, REBCNT in)
{
    assert(sizeof(REBCNT) == 4);
    out[0] = cast(REBYTE, in);
    out[1] = cast(REBYTE, in >> 8);
    out[2] = cast(REBYTE, in >> 16);
    out[3] = cast(REBYTE, in >> 24);
}


//
//  Bytes_To_REBCNT: C
//
// Decode endian-independent sequence of 4 bytes back into a 32-bit unsigned
//
static REBCNT Bytes_To_REBCNT(const REBYTE * const in)
{
    assert(sizeof(REBCNT) == 4);
    return cast(REBCNT, in[0])
        | cast(REBCNT, in[1] << 8)
        | cast(REBCNT, in[2] << 16)
        | cast(REBCNT, in[3] << 24);
}


//
// Zlib has these magic unnamed bit flags which are passed as windowBits:
//
//     "windowBits can also be greater than 15 for optional gzip
//      decoding.  Add 32 to windowBits to enable zlib and gzip
//      decoding with automatic header detection, or add 16 to
//      decode only the gzip format (the zlib format will return
//      a Z_DATA_ERROR)."
//
// Compression obviously can't read your mind to decide what kind you want,
// but decompression can discern non-raw zlib vs. gzip.  It might be useful
// to still be "strict" and demand you to know which kind you have in your
// hand, to make a dependency on gzip explicit (in case you're looking for
// that and want to see if you could use a lighter build without it...)
//
static const int window_bits_zlib = MAX_WBITS;
static const int window_bits_gzip = MAX_WBITS | 16; // "+ 16"
static const int window_bits_detect_zlib_gzip = MAX_WBITS | 32; // "+ 32"
static const int window_bits_zlib_raw = -(MAX_WBITS);
static const int window_bits_gzip_raw = -(MAX_WBITS | 16); // "raw gzip" ?!


//
//  Error_Compression: C
//
// Zlib gives back string error messages.  We use them or fall
// back on the integer code if there is no message.
//
static REBCTX *Error_Compression(const z_stream *strm, int ret)
{
    if (ret == Z_MEM_ERROR) {
        //
        // We do not technically know the amount of memory that zlib asked
        // for and did not get.  Hence categorizing it as an "out of memory"
        // error might be less useful than leaving as a compression error,
        // but that is what the old code here historically did.
        //
        fail (Error_No_Memory(0));
    }

    DECLARE_LOCAL (arg);
    if (strm->msg != NULL)
        Init_String(arg, Make_UTF8_May_Fail(strm->msg));
    else
        Init_Integer(arg, ret);

    return Error_Bad_Compression_Raw(arg);
}


//
//  Compress: C
//
// !!! Adds 32-bit size info to zlib non-raw compressions for compatibility
// with Rebol2 and R3-Alpha, at the cost of inventing yet-another-format.
// Consider removing.
//
REBSER *Compress(
    REBSER *input,
    REBINT index,
    REBCNT len,
    REBOOL gzip,
    REBOOL raw
) {
    int ret;

    assert(BYTE_SIZE(input)); // must be BINARY!

    // compression level can be a value from 1 to 9, or Z_DEFAULT_COMPRESSION
    // if you want it to pick what the library author considers the "worth it"
    // tradeoff of time to generally suggest.
    //
    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;

    // Should there be detection?  (This suppresses unused const warning.)
    //
    UNUSED(window_bits_detect_zlib_gzip);

    ret = deflateInit2(
        &strm,
        Z_DEFAULT_COMPRESSION,
        Z_DEFLATED,
        raw
            ? (gzip ? window_bits_gzip_raw : window_bits_zlib_raw)
            : (gzip ? window_bits_gzip : window_bits_zlib),
        8,
        Z_DEFAULT_STRATEGY
    );

    if (ret != Z_OK)
        fail (Error_Compression(&strm, ret));

    // http://stackoverflow.com/a/4938401/211160
    //
    REBCNT buf_size = deflateBound(&strm, len);

    strm.avail_in = len;
    strm.next_in = BIN_HEAD(input) + index;

    REBSER *output = Make_Binary(buf_size);
    strm.avail_out = buf_size;
    strm.next_out = BIN_HEAD(output);

    ret = deflate(&strm, Z_FINISH);
    deflateEnd(&strm);

    if (ret != Z_STREAM_END)
        fail (Error_Compression(&strm, ret));

    TERM_BIN_LEN(output, buf_size - strm.avail_out);

    if (gzip) {
    #if !defined(NDEBUG)
        //
        // GZIP contains its own CRC.  It also has a 32-bit uncompressed
        // length, conveniently (and perhaps confusingly) at the tail in the
        // same format that R3-Alpha and Rebol2 used.

        REBCNT gzip_len = Bytes_To_REBCNT(
            SER_DATA_RAW(output)
            + buf_size
            - strm.avail_out
            - sizeof(REBCNT)
        );
        assert(len == gzip_len);
    #endif
    }
    else if (!raw) {
        //
        // Add 32-bit length to the end.
        //
        // !!! In ZLIB format the length can be found by decompressing, but
        // not known a priori.  So this is for efficiency.  It would likely be
        // better to not include this as it only confuses matters for those
        // expecting the data to be in a known format...though it means that
        // clients who wanted to decompress to a known allocation size would
        // have to save the size somewhere.
        //
        REBYTE out_size[sizeof(REBCNT)];
        REBCNT_To_Bytes(out_size, cast(REBCNT, len));
        Append_Series(output, cast(REBYTE*, out_size), sizeof(REBCNT));
    }

    // !!! Trim if more than 1K extra capacity, review logic
    //
    if (SER_AVAIL(output) > 1024) {
        REBSER *smaller = Copy_Sequence(output);
        Free_Series(output);
        output = smaller;
    }

    return output;
}


//
//  Decompress: C
//
REBSER *Decompress(
    const REBYTE *input,
    REBCNT len,
    REBINT max,
    REBOOL gzip,
    REBOOL raw
) {
    int ret;

    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.total_out = 0;

    REBCNT buf_size;
    if (gzip || !raw) {
        //
        // Both gzip and Rebol's envelope have the size living in the last
        // 4 bytes of the payload.
        //
        assert(sizeof(REBCNT) == 4);
        if (len <= sizeof(REBCNT)) {
            // !!! Better error message needed
            fail (Error_Past_End_Raw());
        }
        buf_size = Bytes_To_REBCNT(input + len - sizeof(REBCNT));

        // If we know the size is too big go ahead and report an error
        // before doing the buffer allocation
        //
        if (max >= 0 && buf_size > cast(REBCNT, max)) {
            DECLARE_LOCAL (temp);
            Init_Integer(temp, max);

            // NOTE: You can hit this if you 'make prep' without doing a full
            // rebuild.  'make clean' and build again, it should go away.
            //
            fail (Error_Size_Limit_Raw(temp));
        }
    }
    else {
        // We need some logic for dealing with guessing the size of a zlib
        // compression when there's no header.  There is no way a priori to
        // know what that size will be:
        //
        //     http://stackoverflow.com/q/929757/211160
        //
        // If the user's pass in for the "max" seems in the ballpark of a
        // compression ratio (as opposed to some egregious large number)
        // then use it, because often that will be the exact size.
        //
        // If the guess is wrong, then the decompression has to keep making
        // a bigger buffer and trying to continue.  Better heuristics welcome.

        // "Typical zlib compression ratios are from 1:2 to 1:5"

        if (max >= 0 && (cast(REBCNT, max) < len * 6))
            buf_size = max;
        else
            buf_size = len * 3;
    }

    // We only subtract out the double-checking size if this came from a
    // zlib compression without /ONLY.
    //
    strm.avail_in = (!raw && !gzip) ? len - sizeof(REBCNT) : len;
    strm.next_in = input;

    // !!! Zlib can detect decompression...use window_bits_detect_zlib_gzip?
    //
    ret = inflateInit2(
        &strm,
        raw
            ? (gzip ? window_bits_gzip_raw : window_bits_zlib_raw)
            : (gzip ? window_bits_gzip : window_bits_zlib)
    );
    if (ret != Z_OK)
        fail (Error_Compression(&strm, ret));

    // Zlib internally allocates state which must be freed, and is not series
    // memory.  *But* the following code is a mixture of Zlib code and Rebol
    // code (e.g. Extend_Series may run out of memory).  If any error is
    // raised, a longjmp skips `inflateEnd()` and the Zlib state is leaked,
    // ruining the pristine Valgrind output.
    //
    // Since we do the trap anyway, this is the way we handle explicit errors
    // called in the code below also.
    //
    struct Reb_State state;
    REBCTX *error;

    PUSH_UNHALTABLE_TRAP(&error, &state);

// The first time through the following code 'error' will be NULL, but...
// `fail` can longjmp here, so 'error' won't be NULL *if* that happens!

    if (error) {
        //
        // output will already have been freed
        //
        inflateEnd(&strm);
        fail (error);
    }

    // Since the initialization succeeded, go ahead and make the output buffer
    //
    REBSER *output = Make_Binary(buf_size);
    strm.avail_out = buf_size;
    strm.next_out = BIN_HEAD(output);

    // Loop through and allocate a larger buffer each time we find the
    // decompression did not run to completion.  Stop if we exceed max.
    //
    while (TRUE) {

        // Perform the inflation
        //
        ret = inflate(&strm, Z_NO_FLUSH);

        if (ret == Z_STREAM_END) {
            //
            // Finished with the buffer being big enough...
            //
            break;
        }

        if (ret != Z_OK)
            fail (Error_Compression(&strm, ret));

        // Still more data to come.  Use remaining data amount to guess
        // size to add.
        //
        REBCNT old_size = buf_size;

        if (max >= 0 && buf_size >= cast(REBCNT, max)) {
            DECLARE_LOCAL (temp);
            Init_Integer(temp, max);

            // NOTE: You can hit this on 'make prep' without doing a full
            // rebuild.  'make clean' and build again, it should go away.
            //
            fail (Error_Size_Limit_Raw(temp));
        }

        buf_size = buf_size + strm.avail_in * 3;
        if (max >= 0 && buf_size > cast(REBCNT, max))
            buf_size = max;

        assert(strm.avail_out == 0); // !!! is this guaranteed?
        assert(
            strm.next_out == BIN_HEAD(output) + old_size - strm.avail_out
        );

        Extend_Series(output, buf_size - old_size);

        // Extending keeps the content but may realloc the pointer, so
        // put it at the same spot to keep writing to
        //
        strm.next_out = BIN_HEAD(output) + old_size - strm.avail_out;

        strm.avail_out += buf_size - old_size;
    }

    TERM_BIN_LEN(output, strm.total_out);

    // !!! Trim if more than 1K extra capacity, review logic
    //
    if (SER_AVAIL(output) > 1024) {
        REBSER *smaller = Copy_Sequence(output);
        Free_Series(output);
        output = smaller;
    }

    DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

    // Make this the last thing done so strm variables can be read up to end
    //
    inflateEnd(&strm);

    return output;
}
