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
// Copyright 2012-2018 Rebol Open Source Contributors
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
// Options are offered for using zlib envelope, gzip envelope, or raw deflate.
//
// !!! zlib is designed to do streaming compression.  While that code is
// part of the linked in library, it's not exposed by this interface.
//
// !!! Since the zlib code/API isn't actually modified, one could dynamically
// link to a zlib on the platform instead of using the extracted version.
//

#include "sys-core.h"
#include "sys-zlib.h"


//
//  Bytes_To_U32_BE: C
//
// Decode bytes in Big Endian format (least significant byte first) into a
// uint32.  GZIP format uses this to store the decompressed-size-mod-2^32.
//
static uint32_t Bytes_To_U32_BE(const Byte * const in)
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
// "raw gzip" would be nonsense, e.g. `-(MAX_WBITS | 16)`


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


// Zlib gives back string error messages.  We use them or fall back on the
// integer code if there is no message.
//
static REBCTX *Error_Compression(const z_stream *strm, int ret)
{
    // rebMalloc() fails vs. returning nullptr, so as long as zalloc() is used
    // then Z_MEM_ERROR should never happen.
    //
    assert(ret != Z_MEM_ERROR);

    DECLARE_VALUE (arg);
    if (strm->msg)
        Init_Text(arg, Make_String_UTF8(strm->msg));
    else
        Init_Integer(arg, ret);

    return Error_Bad_Compression_Raw(arg);
}


//
//  Compress_Alloc_Core: C
//
// Common code for compressing raw deflate, zlib envelope, gzip envelope.
// Exported as rebDeflateAlloc() and rebGunzipAlloc() for clarity.
//
unsigned char *Compress_Alloc_Core(
    size_t *out_len,
    const void* input,
    size_t in_len,
    Symbol* envelope // NONE, ZLIB, or GZIP... null defaults GZIP
){
    z_stream strm;
    strm.zalloc = &zalloc; // fail() cleans up automatically, see notes
    strm.zfree = &zfree;
    strm.opaque = nullptr; // passed to zalloc and zfree, not needed currently

    int window_bits = window_bits_gzip;
    if (not envelope) {
        //
        // See notes in Decompress_Alloc_Core() about why gzip is chosen to
        // be invocable via nullptr for bootstrap; not really applicable to
        // the compression side, but might as well be consistent.
    }
    else switch (Symbol_Id(envelope)) {
      case SYM_NONE:
        window_bits = window_bits_zlib_raw;
        break;

      case SYM_ZLIB:
        window_bits = window_bits_zlib;
        break;

      case SYM_GZIP:
        window_bits = window_bits_gzip;
        break;

      default:
        assert(false); // release build keeps default
    }

    // compression level can be a value from 1 to 9, or Z_DEFAULT_COMPRESSION
    // if you want it to pick what the library author considers the "worth it"
    // tradeoff of time to generally suggest.
    //
    int ret_init = deflateInit2(
        &strm,
        Z_DEFAULT_COMPRESSION,
        Z_DEFLATED,
        window_bits,
        8,
        Z_DEFAULT_STRATEGY
    );
    if (ret_init != Z_OK)
        fail (Error_Compression(&strm, ret_init));

    // http://stackoverflow.com/a/4938401
    //
    REBLEN buf_size = deflateBound(&strm, in_len);

    strm.avail_in = in_len;
    strm.next_in = cast(const z_Bytef*, input);

    Byte *output = rebAllocN(Byte, buf_size);
    strm.avail_out = buf_size;
    strm.next_out = output;

    int ret_deflate = deflate(&strm, Z_FINISH);
    if (ret_deflate != Z_STREAM_END)
        fail (Error_Compression(&strm, ret_deflate));

    assert(strm.total_out == buf_size - strm.avail_out);
    if (out_len)
        *out_len = strm.total_out;

  #if !defined(NDEBUG)
    //
    // GZIP contains a 32-bit length of the uncompressed data (modulo 2^32),
    // at the tail of the compressed data.  Sanity check that it's right.
    //
    if (envelope and Symbol_Id(envelope) == SYM_GZIP) {
        uint32_t gzip_len = Bytes_To_U32_BE(
            output + strm.total_out - sizeof(uint32_t)
        );
        assert(in_len == gzip_len); // !!! 64-bit REBLEN would need modulo
    }
  #endif

    // !!! Trim if more than 1K extra capacity, review logic
    //
    assert(buf_size >= strm.total_out);
    if (buf_size - strm.total_out > 1024)
        output = cast(Byte*, rebRealloc(output, strm.total_out));

    deflateEnd(&strm);
    return output; // done last (so strm variables can be read up to end)
}


//
//  Decompress_Alloc_Core: C
//
// Common code for decompressing: raw deflate, zlib envelope, gzip envelope.
// Exported as rebInflateAlloc() and rebGunzipAlloc() for clarity.
//
unsigned char *Decompress_Alloc_Core(
    size_t *len_out,
    const void *input,
    size_t len_in,
    int max,
    Symbol* envelope // NONE, ZLIB, GZIP, or DETECT... null defaults GZIP
){
    z_stream strm;
    strm.zalloc = &zalloc; // fail() cleans up automatically, see notes
    strm.zfree = &zfree;
    strm.opaque = nullptr; // passed to zalloc and zfree, not needed currently
    strm.total_out = 0;

    strm.avail_in = len_in;
    strm.next_in = cast(const z_Bytef*, input);

    int window_bits = window_bits_gzip;
    if (not envelope) {
        //
        // The reason GZIP is chosen as the default is because the symbols
        // in %words.r are loaded as part of the boot process from code that
        // is compressed with GZIP, so it's a Catch-22 otherwise.
    }
    else switch (Symbol_Id(envelope)) {
      case SYM_NONE:
        window_bits = window_bits_zlib_raw;
        break;

      case SYM_ZLIB:
        window_bits = window_bits_zlib;
        break;

      case SYM_GZIP:
        window_bits = window_bits_gzip;
        break;

      case SYM_DETECT:
        window_bits = window_bits_detect_zlib_gzip;
        break;

      default:
        assert(false); // fall through with default in release build
    }

    int ret_init = inflateInit2(&strm, window_bits);
    if (ret_init != Z_OK)
        fail (Error_Compression(&strm, ret_init));

    REBLEN buf_size;
    if (
        envelope
        and Symbol_Id(envelope) == SYM_GZIP // not DETECT...trust stored size
        and len_in < 4161808 // (2^32 / 1032 + 18) ->1032 is max deflate ratio
    ){
        const Size gzip_min_overhead = 18; // at *least* 18 bytes
        if (len_in < gzip_min_overhead)
            fail ("GZIP compressed size less than minimum for gzip format");

        // Size (modulo 2^32) is in the last 4 bytes, *if* it's trusted:
        //
        // see http://stackoverflow.com/a/9213826
        //
        // Note that since it's not known how much actual gzip header info
        // there is, it's not possible to tell if a very small number here
        // (compared to the input data) is actually wrong.
        //
        buf_size = Bytes_To_U32_BE(
            cast(const Byte*, input) + len_in - sizeof(uint32_t)
        );
    }
    else {
        // Zlib envelope does not store decompressed size, have to guess:
        //
        // http://stackoverflow.com/q/929757/211160
        //
        // Gzip envelope may *ALSO* need guessing if the data comes from a
        // sketchy source (GNU gzip utilities are, unfortunately, sketchy).
        // Use SYM_DETECT instead of SYM_GZIP with untrusted gzip sources:
        //
        // http://stackoverflow.com/a/9213826
        //
        // If the passed-in "max" seems in the ballpark of a compression ratio
        // then use it, because often that will be the exact size.
        //
        // If the guess is wrong, then the decompression has to keep making
        // a bigger buffer and trying to continue.  Better heuristics welcome.

        // "Typical zlib compression ratios are from 1:2 to 1:5"

        if (max >= 0 and (cast(REBLEN, max) < len_in * 6))
            buf_size = max;
        else
            buf_size = len_in * 3;
    }

    // Use memory backed by a managed series (can be converted to a series
    // later if desired, via Rebserize)
    //
    Byte *output = rebAllocN(Byte, buf_size);
    strm.avail_out = buf_size;
    strm.next_out = cast(Byte*, output);

    // Loop through and allocate a larger buffer each time we find the
    // decompression did not run to completion.  Stop if we exceed max.
    //
    while (true) {
        int ret_inflate = inflate(&strm, Z_NO_FLUSH);

        if (ret_inflate == Z_STREAM_END)
            break; // Finished. (and buffer was big enough)

        if (ret_inflate != Z_OK)
            fail (Error_Compression(&strm, ret_inflate));

        // Note: `strm.avail_out` isn't necessarily 0 here, first observed
        // with `inflate #{AAAAAAAAAAAAAAAAAAAA}` (which is bad, but still)
        //
        assert(strm.next_out == output + buf_size - strm.avail_out);

        if (max >= 0 and buf_size >= cast(REBLEN, max)) {
            DECLARE_VALUE (temp);
            Init_Integer(temp, max);
            fail (Error_Size_Limit_Raw(temp));
        }

        // Use remaining input amount to guess how much more decompressed
        // data might be produced.  Clamp to limit.
        //
        REBLEN old_size = buf_size;
        buf_size = buf_size + strm.avail_in * 3;
        if (max >= 0 and buf_size > cast(REBLEN, max))
            buf_size = max;

        output = cast(Byte*, rebRealloc(output, buf_size));

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
        output = cast(Byte*, rebRealloc(output, strm.total_out));

    if (len_out)
        *len_out = strm.total_out;

    inflateEnd(&strm); // done last (so strm variables can be read up to end)
    return output;
}
