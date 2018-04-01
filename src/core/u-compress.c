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
// Classically, Rebol added a 32-bit size header onto compressed data,
// indicating the uncompressed size.  This is the default BINARY! format
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


#include "mem-series.h" // !!! needed for BIAS adjustment, should we export?


//
//  U32_To_Bytes: C
//
// Get endian-independent encoding of a 32-bit unsigned integer to 4 bytes
//
static void U32_To_Bytes(REBYTE *out, uint32_t in)
{
    out[0] = cast(REBYTE, in);
    out[1] = cast(REBYTE, in >> 8);
    out[2] = cast(REBYTE, in >> 16);
    out[3] = cast(REBYTE, in >> 24);
}


//
//  Bytes_To_U32: C
//
// Decode endian-independent sequence of 4 bytes back into a 32-bit unsigned
//
static uint32_t Bytes_To_U32(const REBYTE * const in)
{
    return cast(uint32_t, in[0])
        | cast(uint32_t, in[1] << 8)
        | cast(uint32_t, in[2] << 16)
        | cast(uint32_t, in[3] << 24);
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


// Inflation and deflation tends to ultimately target series, so we want to
// be using memory that can be transitioned to a series without reallocation.
// See rebRepossess() for how rebMalloc()'d pointers can be used this way.
//
// We go ahead and use the rebMalloc() for zlib's internal state allocation
// too, so that any fail() calls (e.g. out-of-memory during a rebRealloc())
// will automatically free that state.  Thus inflateEnd() and deflateEnd()
// only need to be called if there is no failure.  There's no need to
// rebRescue(), clean up, and rethrow the error.
//
// As a side-benefit, fail() can be used freely for other errors during the
// inflate or deflate.

static void *zalloc(void *opaque, unsigned nr, unsigned size)
{
    UNUSED(opaque);
    return rebMalloc(nr * size);
}

static void zfree(void *opaque, void *addr)
{
    UNUSED(opaque);
    rebFree(addr);
}


//
//  Error_Compression: C
//
// Zlib gives back string error messages.  We use them or fall back on the
// integer code if there is no message.
//
static REBCTX *Error_Compression(const z_stream *strm, int ret)
{
    // rebMalloc() fails vs. returning NULL, so as long as zalloc() is used
    // then Z_MEM_ERROR should never happen.
    //
    assert(ret != Z_MEM_ERROR);

    DECLARE_LOCAL (arg);
    if (strm->msg != NULL)
        Init_String(arg, Make_UTF8_May_Fail(cb_cast(strm->msg)));
    else
        Init_Integer(arg, ret);

    return Error_Bad_Compression_Raw(arg);
}


//
//  rebDeflateAlloc: C
//
// !!! Currently all RL_API functions are in %a-lib.c, so this isn't actually
// in the external API so long as it lives in %u-compress.c. 
//
// Exposure of the deflate() of the built-in zlib, so that extensions (such as
// a PNG encoder) can reuse it.  Currently this does not take any options for
// tuning the compression, and just uses the recommended defaults by zlib.
//
// See notes on rebMalloc() for how the result can be converted to a series.
//
// !!! Adds 32-bit size info to zlib non-raw compressions for compatibility
// with Rebol2 and R3-Alpha, at the cost of inventing yet-another-format.
// Consider removing.
//
REBYTE *rebDeflateAlloc(
    REBCNT *out_len,
    const unsigned char* input,
    REBCNT in_len,
    REBOOL gzip,
    REBOOL raw,
    REBOOL only
){
    // compression level can be a value from 1 to 9, or Z_DEFAULT_COMPRESSION
    // if you want it to pick what the library author considers the "worth it"
    // tradeoff of time to generally suggest.
    //
    z_stream strm;
    strm.zalloc = &zalloc; // fail() cleans up automatically, see notes
    strm.zfree = &zfree;
    strm.opaque = NULL; // passed to zalloc and zfree, not needed currently

    // Should there be detection?  (This suppresses unused const warning.)
    //
    UNUSED(window_bits_detect_zlib_gzip);

    int ret_init = deflateInit2(
        &strm,
        Z_DEFAULT_COMPRESSION,
        Z_DEFLATED,
        raw
            ? (gzip ? window_bits_gzip_raw : window_bits_zlib_raw)
            : (gzip ? window_bits_gzip : window_bits_zlib),
        8,
        Z_DEFAULT_STRATEGY
    );

    if (ret_init != Z_OK)
        fail (Error_Compression(&strm, ret_init));

    // http://stackoverflow.com/a/4938401
    //
    REBCNT buf_size = deflateBound(&strm, in_len);
    if (NOT(gzip) && NOT(only))
        buf_size += sizeof(uint32_t); // 32-bit length (gzip already added)

    strm.avail_in = in_len;
    strm.next_in = input;

    REBYTE *output = cast(REBYTE*, rebMalloc(buf_size));
    strm.avail_out = buf_size;
    strm.next_out = output;

    int ret_deflate = deflate(&strm, Z_FINISH);
    deflateEnd(&strm);

    if (ret_deflate != Z_STREAM_END)
        fail (Error_Compression(&strm, ret_deflate));

    assert(strm.total_out == buf_size - strm.avail_out);
    REBCNT overall_size; // size after any extra envelope data is added

    if (NOT(gzip) && NOT(only)) {
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
        assert(strm.avail_out >= sizeof(uint32_t));
        U32_To_Bytes(output + strm.total_out, cast(uint32_t, in_len));
        overall_size = strm.total_out + sizeof(uint32_t);
    }
    else {
      #if !defined(NDEBUG)
        //
        // GZIP contains its own CRC.  It also has a 32-bit uncompressed
        // length, conveniently (and perhaps confusingly) at the tail in the
        // same format that R3-Alpha and Rebol2 used.  Double-check it.
        //
        if (gzip) {
            uint32_t gzip_len = Bytes_To_U32(
                output + strm.total_out - sizeof(uint32_t)
            );
            assert(in_len == gzip_len);
        }
      #endif

        overall_size = strm.total_out;
    }

    if (out_len != NULL)
        *out_len = overall_size;

    // !!! Trim if more than 1K extra capacity, review logic
    //
    assert(buf_size >= overall_size);
    if (buf_size - overall_size > 1024)
        output = cast(REBYTE*, rebRealloc(output, strm.avail_out));

    return output;
}


//
//  rebInflateAlloc: C
//
// !!! Currently all RL_API functions are in %a-lib.c, so this isn't actually
// in the external API so long as it lives in %u-compress.c. 
//
// Exposure of the inflate() of the built-in zlib, so that extensions (such as
// a PNG decoder) can reuse it.  Currently this does not take any options for
// tuning the decompression, and just uses the recommended defaults by zlib.
//
// See notes on rebMalloc() for how the result can be converted to a series.
//
REBYTE *rebInflateAlloc(
    REBCNT *len_out,
    const REBYTE *input,
    REBCNT len_in,
    REBINT max,
    REBOOL gzip,
    REBOOL raw,
    REBOOL only // don't add 4-byte size to end
){
    z_stream strm;
    strm.zalloc = &zalloc; // fail() cleans up automatically, see notes
    strm.zfree = &zfree;
    strm.opaque = NULL; // passed to zalloc and zfree, not needed currently
    strm.total_out = 0;

    // We only subtract out the double-checking size if this came from a
    // zlib compression without /ONLY.
    //
    strm.avail_in = only || gzip ? len_in : len_in - sizeof(REBCNT);
    strm.next_in = input;

    // !!! Zlib can detect decompression...use window_bits_detect_zlib_gzip?
    //
    int ret_init = inflateInit2(
        &strm,
        raw
            ? (gzip ? window_bits_gzip_raw : window_bits_zlib_raw)
            : (gzip ? window_bits_gzip : window_bits_zlib)
    );
    if (ret_init != Z_OK)
        fail (Error_Compression(&strm, ret_init));

    REBCNT buf_size;
    if (gzip || NOT(only)) {
        //
        // Both gzip and Rebol's envelope have the uncompressed size living in
        // the last 4 bytes of the payload.
        //
        if (len_in <= sizeof(uint32_t))
            fail (Error_Past_End_Raw()); // !!! Better error?

        buf_size = Bytes_To_U32(input + len_in - sizeof(uint32_t));

        // If we know the size is too big go ahead and report an error
        // before doing the buffer allocation
        //
        if (max >= 0 && buf_size > cast(uint32_t, max)) {
            DECLARE_LOCAL (temp);
            Init_Integer(temp, max);
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
        // If the passed-in "max" seems in the ballpark of a compression ratio
        // then use it, because often that will be the exact size.
        //
        // If the guess is wrong, then the decompression has to keep making
        // a bigger buffer and trying to continue.  Better heuristics welcome.

        // "Typical zlib compression ratios are from 1:2 to 1:5"

        if (max >= 0 && (cast(REBCNT, max) < len_in * 6))
            buf_size = max;
        else
            buf_size = len_in * 3;
    }

    // Use memory backed by a managed series (can be converted to a series
    // later if desired, via Rebserize)
    //
    REBYTE *output = cast(REBYTE*, rebMalloc(buf_size));
    strm.avail_out = buf_size;
    strm.next_out = cast(REBYTE*, output);

    // Loop through and allocate a larger buffer each time we find the
    // decompression did not run to completion.  Stop if we exceed max.
    //
    while (TRUE) {
        int ret_inflate = inflate(&strm, Z_NO_FLUSH);

        if (ret_inflate == Z_STREAM_END)
            break; // Finished. (and buffer was big enough)

        if (ret_inflate != Z_OK)
            fail (Error_Compression(&strm, ret_inflate));

        assert(strm.avail_out == 0); // !!! is this guaranteed?
        assert(strm.next_out == output + buf_size - strm.avail_out);

        if (max >= 0 && buf_size >= cast(REBCNT, max)) {
            DECLARE_LOCAL (temp);
            Init_Integer(temp, max);
            fail (Error_Size_Limit_Raw(temp));
        }

        // Use remaining input amount to guess how much more decompressed
        // data might be produced.  Clamp to limit.
        //
        REBCNT old_size = buf_size;
        buf_size = buf_size + strm.avail_in * 3;
        if (max >= 0 && buf_size > cast(REBCNT, max))
            buf_size = max;

        output = cast(REBYTE*, rebRealloc(output, buf_size));

        // Extending keeps the content but may realloc the pointer, so
        // put it at the same spot to keep writing to
        //
        strm.next_out = output + old_size - strm.avail_out;
        strm.avail_out += buf_size - old_size;
    }

    // !!! Trim if more than 1K extra capacity, review the necessity of this.
    // (Note it won't happen if the caller knew the decompressed size, so
    // e.g. decompression on boot isn't wasting time with this realloc.)
    //
    assert(buf_size >= strm.total_out);
    if (strm.total_out - buf_size > 1024)
        output = cast(REBYTE*, rebRealloc(output, strm.total_out));

    if (len_out != NULL)
        *len_out = strm.total_out;

    inflateEnd(&strm); // done last (so strm variables can be read up to end)
    return output;
}
