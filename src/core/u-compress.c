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
// Copyright 2012-2018 Ren-C Open Source Contributors
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
static uint32_t Bytes_To_U32_BE(const REBYTE *bp)
{
    return cast(uint32_t, bp[0])
        | cast(uint32_t, bp[1] << 8)
        | cast(uint32_t, bp[2] << 16)
        | cast(uint32_t, bp[3] << 24);
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
static const int window_bits_gzip = MAX_WBITS | 16;  // "+ 16"
static const int window_bits_detect_zlib_gzip = MAX_WBITS | 32;  // "+ 32"
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

    DECLARE_LOCAL (arg);
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
REBYTE *Compress_Alloc_Core(
    REBSIZ *size_out,
    const void* input,
    REBSIZ size_in,
    enum Reb_Symbol_Id envelope  // SYM_NONE, SYM_ZLIB, or SYM_GZIP
){
    z_stream strm;
    strm.zalloc = &zalloc;  // fail() cleans up automatically, see notes
    strm.zfree = &zfree;
    strm.opaque = nullptr;  // passed to zalloc/zfree, not needed currently

    int window_bits = window_bits_gzip;
    switch (envelope) {
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
        assert(false);  // release build keeps default
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
    REBLEN buf_size = deflateBound(&strm, size_in);

    strm.avail_in = size_in;
    strm.next_in = cast(const z_Bytef*, input);

    REBYTE *output = rebAllocN(REBYTE, buf_size);
    strm.avail_out = buf_size;
    strm.next_out = output;

    int ret_deflate = deflate(&strm, Z_FINISH);
    if (ret_deflate != Z_STREAM_END)
        fail (Error_Compression(&strm, ret_deflate));

    assert(strm.total_out == buf_size - strm.avail_out);
    if (size_out)
        *size_out = strm.total_out;

  #if !defined(NDEBUG)
    //
    // GZIP contains a 32-bit length of the uncompressed data (modulo 2^32),
    // at the tail of the compressed data.  Sanity check that it's right.
    //
    if (envelope and envelope == SYM_GZIP) {
        uint32_t gzip_len = Bytes_To_U32_BE(
            output + strm.total_out - sizeof(uint32_t)
        );
        assert(size_in == gzip_len);  // !!! 64-bit REBLEN would need modulo
    }
  #endif

    // !!! Trim if more than 1K extra capacity, review logic
    //
    assert(buf_size >= strm.total_out);
    if (buf_size - strm.total_out > 1024)
        output = cast(REBYTE*, rebRealloc(output, strm.total_out));

    deflateEnd(&strm);  // done last (so strm variables can be read up to end)
    return output;
}


//
//  Decompress_Alloc_Core: C
//
// Common code for decompressing: raw deflate, zlib envelope, gzip envelope.
// Exported as rebInflateAlloc() and rebGunzipAlloc() for clarity.
//
REBYTE *Decompress_Alloc_Core(
    REBSIZ *size_out,
    const void *input,
    REBSIZ size_in,
    int max,
    enum Reb_Symbol_Id envelope  // SYM_NONE, SYM_ZLIB, SYM_GZIP, or SYM_DETECT
){
    z_stream strm;
    strm.zalloc = &zalloc;  // fail() cleans up automatically, see notes
    strm.zfree = &zfree;
    strm.opaque = nullptr;  // passed to zalloc/zfree, not needed currently
    strm.total_out = 0;

    strm.avail_in = size_in;
    strm.next_in = cast(const z_Bytef*, input);

    int window_bits = window_bits_gzip;
    switch (envelope) {
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
        assert(false);  // fall through with default in release build
    }

    int ret_init = inflateInit2(&strm, window_bits);
    if (ret_init != Z_OK)
        fail (Error_Compression(&strm, ret_init));

    REBLEN buf_size;
    if (
        envelope == SYM_GZIP  // not DETECT, trust stored size
        and size_in < 4161808  // (2^32 / 1032 + 18) ->1032 max deflate ratio
    ){
        const REBSIZ gzip_min_overhead = 18;  // at *least* 18 bytes
        if (size_in < gzip_min_overhead)
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
            cast(const REBYTE*, input) + size_in - sizeof(uint32_t)
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

        if (max >= 0 and (cast(REBLEN, max) < size_in * 6))
            buf_size = max;
        else
            buf_size = size_in * 3;
    }

    // Use memory backed by a managed series (can be converted to a series
    // later if desired, via Rebserize)
    //
    REBYTE *output = rebAllocN(REBYTE, buf_size);
    strm.avail_out = buf_size;
    strm.next_out = cast(REBYTE*, output);

    // Loop through and allocate a larger buffer each time we find the
    // decompression did not run to completion.  Stop if we exceed max.
    //
    while (true) {
        int ret_inflate = inflate(&strm, Z_NO_FLUSH);

        if (ret_inflate == Z_STREAM_END)
            break;  // Finished. (and buffer was big enough)

        if (ret_inflate != Z_OK)
            fail (Error_Compression(&strm, ret_inflate));

        // Note: `strm.avail_out` isn't necessarily 0 here, first observed
        // with `inflate #{AAAAAAAAAAAAAAAAAAAA}` (which is bad, but still)
        //
        assert(strm.next_out == output + buf_size - strm.avail_out);

        if (max >= 0 and buf_size >= cast(REBLEN, max)) {
            DECLARE_LOCAL (temp);
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

    if (size_out)
        *size_out = strm.total_out;

    inflateEnd(&strm);  // done last (so strm variables can be read up to end)
    return output;
}


//
//  checksum-core: native [
//
//  {Built-in checksums from zlib (see CHECKSUM in Crypt extension for more)}
//
//      return: "Little-endian format of 4-byte CRC-32"
//          [binary!]
//      method "Either ADLER32 or CRC32"
//          [word!]
//      data "Data to encode (using UTF-8 if TEXT!)"
//          [binary! text!]
//      /part "Length of data"
//          [any-value!]
//  ]
//
REBNATIVE(checksum_core)
//
// Most checksum and hashing algorithms are optional in the build (at time of
// writing they are all in the "Crypt" extension).  This is because they come
// in and out of fashion (MD5 and SHA1, for instance), so it doesn't make
// sense to force every build configuration to build them in.
//
// But CRC32 is used by zlib (for gzip, gunzip, and the PKZIP .zip file
// usermode code) and ADLER32 is used for zlib encodings in PNG and such.
// It's a sunk cost to export them.  However, some builds may not want both
// of these either--so bear that in mind.  (ADLER32 is only really needed for
// PNG decoding, I believe (?))
{
    INCLUDE_PARAMS_OF_CHECKSUM_CORE;

    REBLEN len = Part_Len_May_Modify_Index(ARG(data), ARG(part));

    REBSIZ size;
    const REBYTE *data = VAL_BYTES_LIMIT_AT(&size, ARG(data), len);

    uLong crc;  // Note: zlib.h defines "crc32" as "z_crc32"
    switch (VAL_WORD_ID(ARG(method))) {
      case SYM_CRC32:
        crc = crc32_z(0L, data, size);
        break;

      case SYM_ADLER32:
        //
        // The zlib documentation shows passing 0L, but this is not right.
        // "At the beginning [of Adler-32], A is initialized to 1, B to 0"
        // A is the low 16-bits, B is the high.  Hence start with 1L.
        //
        crc = z_adler32(1L, data, size);
        break;

      default:
        fail ("METHOD for CHECKSUM-CORE must be CRC32 or ADLER32");
    }

    REBBIN *bin = Make_Binary(4);
    REBYTE *bp = BIN_HEAD(bin);

    // Returning as a BINARY! avoids signedness issues (R3-Alpha CRC-32 was a
    // signed integer, which was weird):
    //
    // https://github.com/rebol/rebol-issues/issues/2375
    //
    // When formulated as a binary, most callers seem to want little endian.
    //
    int i;
    for (i = 0; i < 4; ++i, ++bp) {
        *bp = crc % 256;
        crc >>= 8;
    }
    TERM_BIN_LEN(bin, 4);

    return Init_Binary(OUT, bin);
}


//
//  deflate: native [
//
//  "Compress data using DEFLATE: https://en.wikipedia.org/wiki/DEFLATE"
//
//      return: [binary!]
//      data "If text, it will be UTF-8 encoded"
//          [binary! text!]
//      /part "Length of data (elements)"
//          [any-value!]
//      /envelope "ZLIB (adler32, no size) or GZIP (crc32, uncompressed size)"
//          [word!]
//  ]
//
REBNATIVE(deflate)
{
    INCLUDE_PARAMS_OF_DEFLATE;

    REBLEN limit = Part_Len_May_Modify_Index(ARG(data), ARG(part));

    REBSIZ size;
    const REBYTE *bp = VAL_BYTES_LIMIT_AT(&size, ARG(data), limit);

    enum Reb_Symbol_Id envelope;
    if (not REF(envelope))
        envelope = SYM_NONE;
    else {
        envelope = cast(enum Reb_Symbol_Id, VAL_WORD_ID(ARG(envelope)));
        switch (envelope) {
          case SYM_ZLIB:
          case SYM_GZIP:
            break;

          default:
            fail (PAR(envelope));
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
//      data [binary! handle!]
//      /part "Length of compressed data (must match end marker)"
//          [any-value!]
//      /max "Error out if result is larger than this"
//          [integer!]
//      /envelope "ZLIB, GZIP, or DETECT (http://stackoverflow.com/a/9213826)"
//          [word!]
//  ]
//
REBNATIVE(inflate)
//
// GZIP is a slight variant envelope which uses a CRC32 checksum.  For data
// whose original size was < 2^32 bytes, the gzip envelope stored that size...
// so memory efficiency is achieved even if max = -1.
//
// Note: That size guarantee exists for data compressed with rebGzipAlloc() or
// adhering to the gzip standard.  However, archives created with the GNU
// gzip tool make streams with possible trailing zeros or concatenations:
//
// http://stackoverflow.com/a/9213826
{
    INCLUDE_PARAMS_OF_INFLATE;

    REBINT max;
    if (REF(max)) {
        max = Int32s(ARG(max), 1);
        if (max < 0)
            fail (PAR(max));
    }
    else
        max = -1;

    const REBYTE *data;
    REBSIZ size;
    if (IS_BINARY(ARG(data))) {
        size = Part_Len_May_Modify_Index(ARG(data), ARG(part));
        data = VAL_BINARY_AT(ARG(data));  // after (in case index modified)
    }
    else {
        size = VAL_HANDLE_LEN(ARG(data));
        data = VAL_HANDLE_POINTER(REBYTE, ARG(data));
    }

    enum Reb_Symbol_Id envelope;
    if (not REF(envelope))
        envelope = SYM_NONE;
    else {
        envelope = cast(enum Reb_Symbol_Id, VAL_WORD_ID(ARG(envelope)));
        switch (envelope) {
          case SYM_ZLIB:
          case SYM_GZIP:
          case SYM_DETECT:
            break;

          default:
            fail (PAR(envelope));
        }
    }

    size_t decompressed_size;
    void *decompressed = Decompress_Alloc_Core(
        &decompressed_size,
        data,
        size,
        max,
        envelope
    );

    return rebRepossess(decompressed, decompressed_size);
}
