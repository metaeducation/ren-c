//
//  file: %u-compress.c
//  summary: "interface to zlib compression"
//  section: utility
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
// Binary Flexes.
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

#undef Byte  // sys-zlib.h defines it compatibly (unsigned char)
#include "sys-zlib.h"


//
//  Bytes_To_U32_BE: C
//
// Decode bytes in Big Endian format (least significant byte first) into a
// uint32.  GZIP format uses this to store the decompressed-size-mod-2^32.
//
static uint32_t Bytes_To_U32_BE(const Byte* bp)
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


// Inflation and deflation tends to ultimately target BLOB!, so we want to
// be using memory that can be transitioned to a BLOB! without reallocation.
// See rebRepossess() for how rebAlloc()'d pointers can be used this way.
//
// We go ahead and use the rebAllocBytes() for zlib's internal state allocation
// too, so that any panic() calls (e.g. out-of-memory during a rebRealloc())
// will automatically free that state.  Thus inflateEnd() and deflateEnd()
// only need to be called if there is no failure.
//
// As a side-benefit, panic() can be used freely for other errors during the
// inflate or deflate.
//
static void* zalloc(void *opaque, unsigned nr, unsigned size)
{
    UNUSED(opaque);
    return rebAllocBytes(nr * size);
}

static void zfree(void *opaque, void *addr)
{
    UNUSED(opaque);
    rebFree(addr);
}


// Zlib gives back string error messages.  We use them or fall back on the
// integer code if there is no message.
//
// 1. rebAlloc() panics vs. returning nullptr, so as long as zalloc() is used
//    then Z_MEM_ERROR should never happen.
//
static Error* Error_Compression(const z_stream *strm, int ret)
{
    assert(ret != Z_MEM_ERROR);  // memory errors should have panic()'d [1]

    DECLARE_ELEMENT (arg);
    if (strm->msg)
        Init_Text(arg, Make_Strand_UTF8(strm->msg));
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
// 1. The memory buffer pointer returned by this routine is allocated using
//    rebAllocN(), and is backed by a managed Flex.  This means it can be
//    converted to a BLOB! if desired, via rebRepossess().  Otherwise it
//    should be freed using rebFree()
//
// 2. GZIP contains a 32-bit length of the uncompressed data (modulo 2^32),
//    at the tail of the compressed data.  Sanity check that it's right.
//
Byte* Compress_Alloc_Core(
    Option(Sink(Size)) size_out,
    const void* input,
    Size size_in,
    Option(SymId) envelope  // SYM_ZLIB, or SYM_GZIP
){
    z_stream strm;
    strm.zalloc = &zalloc;  // panic() will clean up, see zalloc() definition
    strm.zfree = &zfree;
    strm.opaque = nullptr;  // passed to zalloc/zfree, not needed currently

    int window_bits = window_bits_gzip;
    if (not envelope) {
        window_bits = window_bits_zlib_raw;
    }
    else switch (envelope) {
      case SYM_ZLIB:
        window_bits = window_bits_zlib;
        break;

      case SYM_GZIP:
        window_bits = window_bits_gzip;
        break;

      default:
        assert(false);  // release build keeps default
    }

    int ret_init = deflateInit2(
        &strm,
        Z_DEFAULT_COMPRESSION,  // space/time tradeoff (1 to 9), use default
        Z_DEFLATED,
        window_bits,
        8,
        Z_DEFAULT_STRATEGY
    );
    if (ret_init != Z_OK)
        panic (Error_Compression(&strm, ret_init));

    // http://stackoverflow.com/a/4938401
    //
    uLong buf_size = deflateBound(&strm, size_in);  // easier as uLong not Size

    strm.avail_in = size_in;
    strm.next_in = c_cast(z_Bytef*, input);

    Byte* output = rebAllocN(Byte, buf_size);  // can rebRepossess() this [1]
    strm.avail_out = buf_size;
    strm.next_out = output;

    int ret_deflate = deflate(&strm, Z_FINISH);
    if (ret_deflate != Z_STREAM_END)
        panic (Error_Compression(&strm, ret_deflate));

    assert(strm.total_out == buf_size - strm.avail_out);
    if (size_out)
        *(unwrap size_out) = strm.total_out;

  #if RUNTIME_CHECKS
    if (envelope and envelope == SYM_GZIP) {
        uint32_t gzip_len = Bytes_To_U32_BE(  // verify compressed size [2]
            output + strm.total_out - sizeof(uint32_t)
        );
        assert(size_in == cast(Size, gzip_len));  // !!! 64-bit needs modulo
        UNUSED(gzip_len);
    }
  #endif

    // !!! Trim if more than 1K extra capacity, review logic
    //
    assert(buf_size >= strm.total_out);
    if (buf_size - strm.total_out > 1024)
        output = rebReallocBytes(output, strm.total_out);

    deflateEnd(&strm);  // done last (so strm variables can be read up to end)
    return output;
}


//
//  Decompress_Alloc_Core: C
//
// Common code for decompressing: raw deflate, zlib envelope, gzip envelope.
// Exported as rebInflateAlloc() and rebGunzipAlloc() for clarity.
//
// 1. The memory buffer pointer returned by this routine is allocated using
//    rebAllocN(), and is backed by a managed Flex.  This means it can be
//    converted to a BLOB! if desired, via rebRepossess().  Otherwise it
//    should be freed using rebFree()
//
// 2. Size (modulo 2^32) is in the last 4 bytes, *if* it's trusted:
//
//      http://stackoverflow.com/a/9213826
//
//    Note that since it's not known how much actual gzip header info there is,
//    it's not possible to tell if a very small number here (compared to the
//    length of the input data) is actually wrong.
//
// 3. Zlib envelope does not store decompressed size, have to guess:
//
//      http://stackoverflow.com/q/929757/211160
//
//    Gzip envelope may *ALSO* need guessing if the data comes from a sketchy
//    source (GNU gzip utilities are, unfortunately, sketchy).  Use SYM_DETECT
//    instead of SYM_GZIP with untrusted gzip sources:
//
//      http://stackoverflow.com/a/9213826
//
//    If the passed-in "max" seems in the ballpark of a compression ratio
//    then use it, because often that will be the exact size.
//
//    If the guess is wrong, then the decompression has to keep making
//    a bigger buffer and trying to continue.  Better heuristics welcome.
//
//      "Typical zlib compression ratios are from 1:2 to 1:5"
//
Byte* Decompress_Alloc_Core(  // returned pointer can be rebRepossessed() [1]
    Size* size_out,
    const void *input,
    Size size_in,
    int max,
    Option(SymId) envelope  // SYM_0, SYM_ZLIB, SYM_GZIP, or SYM_DETECT
){
    z_stream strm;
    strm.zalloc = &zalloc;  // panic() will clean up, see zalloc() definition
    strm.zfree = &zfree;
    strm.opaque = nullptr;  // passed to zalloc/zfree, not needed currently
    strm.total_out = 0;

    strm.avail_in = size_in;
    strm.next_in = c_cast(z_Bytef*, input);

    int window_bits = window_bits_gzip;
    if (not envelope) {
        window_bits = window_bits_zlib_raw;
    }
    else switch (envelope) {
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
        assert(false);
    }

    int ret_init = inflateInit2(&strm, window_bits);
    if (ret_init != Z_OK)
        panic (Error_Compression(&strm, ret_init));

    uLong buf_size;  // easiest to speak in zlib uLong vs. signed `Size`
    if (
        envelope == SYM_GZIP  // not DETECT, trust stored size
        and size_in < 4161808  // (2^32 / 1032 + 18) ->1032 max deflate ratio
    ){
        const Size gzip_min_overhead = 18;  // at *least* 18 bytes
        if (size_in < gzip_min_overhead)
            panic ("GZIP compressed size less than minimum for gzip format");

        buf_size = Bytes_To_U32_BE(  // size is last 4 bytes [2]
            c_cast(Byte*, input) + size_in - sizeof(uint32_t)
        );
    }
    else {  // no decompressed size in envelope (or untrusted), must guess [3]
        if (max >= 0 and max < size_in * 6)
            buf_size = max;
        else
            buf_size = size_in * 3;
    }

    Byte* output = rebAllocN(Byte, buf_size);  // can rebRepossess() this [1]
    strm.avail_out = buf_size;
    strm.next_out = cast(Byte*, output);

    // Loop through and allocate a larger buffer each time we find the
    // decompression did not run to completion.  Stop if we exceed max.
    //
    while (true) {
        int ret_inflate = inflate(&strm, Z_NO_FLUSH);

        if (ret_inflate == Z_STREAM_END)
            break;  // Finished. (and buffer was big enough)

        if (ret_inflate != Z_OK)
            panic (Error_Compression(&strm, ret_inflate));

        // Note: `strm.avail_out` isn't necessarily 0 here, first observed
        // with `inflate #{AAAAAAAAAAAAAAAAAAAA}` (which is bad, but still)
        //
        assert(strm.next_out == output + buf_size - strm.avail_out);

        if (max >= 0 and Cast_Signed(buf_size) >= max) {
            DECLARE_ELEMENT (temp);
            Init_Integer(temp, max);
            panic (Error_Size_Limit_Raw(temp));
        }

        // Use remaining input amount to guess how much more decompressed
        // data might be produced.  Clamp to limit.
        //
        Size old_size = buf_size;
        buf_size = buf_size + strm.avail_in * 3;
        if (max >= 0 and Cast_Signed(buf_size) > max)
            buf_size = max;

        output = rebReallocBytes(output, buf_size);

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
        output = rebReallocBytes(output, strm.total_out);

    if (size_out)
        *size_out = strm.total_out;

    inflateEnd(&strm);  // done last (so strm variables can be read up to end)
    return output;
}


//
//  checksum-core: native [
//
//  "Built-in checksums from zlib (see CHECKSUM in Crypt extension for more)"
//
//      return: "Little-endian format of 4-byte CRC-32"
//          [blob!]  ; binary return avoids signedness issues [1]
//      method [~(adler32 crc32)~]
//      data "Data to encode (using UTF-8 if TEXT!)"
//          [blob! text!]
//      :part "Length of data"
//          [integer! blob! text!]
//  ]
//
DECLARE_NATIVE(CHECKSUM_CORE)
//
// Most checksum and hashing algorithms are optional in the build (at time of
// writing they are all in the "Crypt" extension).  This is because they come
// in and out of fashion (MD5 and SHA1, for instance), so it doesn't make
// sense to force every configuration to build them in.
//
// But the interpreter core depends on zlib compression.  CRC32 is used by zlib
// (for gzip, gunzip, and the PKZIP .zip file usermode code) and ADLER32 is
// used for zlib encodings in PNG and such.  It's a sunk cost to export them.
// However, some builds may not want both of these either--so bear that in
// mind.  (ADLER32 is only really needed for PNG decoding, I believe (?))
//
// 1. Returning as a BLOB! avoids signedness issues (R3-Alpha CRC-32 was a
//    signed integer, which was weird):
//
//       https://github.com/rebol/rebol-issues/issues/2375
//
//    When formulated as a binary, most callers seem to want little endian.
//
// 2. The zlib documentation shows passing 0L, but this is not right.
//    "At the beginning [of Adler-32], A is initialized to 1, B to 0"
//    A is the low 16-bits, B is the high.  Hence start with 1L.
{
    INCLUDE_PARAMS_OF_CHECKSUM_CORE;

    REBLEN len = Part_Len_May_Modify_Index(ARG(DATA), ARG(PART));

    Size size;
    const Byte* data = Cell_Bytes_Limit_At(&size, ARG(DATA), &len);

    uLong crc;  // Note: zlib.h defines "crc32" as "z_crc32"
    switch (Cell_Word_Id(ARG(METHOD))) {
      case SYM_CRC32:
        crc = crc32_z(0L, data, size);
        break;

      case SYM_ADLER32:
        crc = z_adler32(1L, data, size);  // 1L is right, not 0L, see [2]
        break;

      default:
        crc = 0;  // avoid compiler warning
        assert(!"Bug in typechecking of method parameter");
    }

    Binary* bin = Make_Binary(4);
    Byte* bp = Binary_Head(bin);

    int i;
    for (i = 0; i < 4; ++i, ++bp) {
        *bp = crc % 256;
        crc >>= 8;
    }
    Term_Binary_Len(bin, 4);

    return Init_Blob(OUT, bin);
}


//
//  deflate: native [
//
//  "Compress data using DEFLATE: https://en.wikipedia.org/wiki/DEFLATE"
//
//      return: [blob!]
//      data "If text, it will be UTF-8 encoded"
//          [blob! text!]
//      :part "Length of data (elements)"
//          [integer! blob! text!]
//      :envelope "ZLIB (adler32, no size) or GZIP (crc32, uncompressed size)"
//          [~(zlib gzip)~]
//  ]
//
DECLARE_NATIVE(DEFLATE)
{
    INCLUDE_PARAMS_OF_DEFLATE;

    REBLEN limit = Part_Len_May_Modify_Index(ARG(DATA), ARG(PART));

    Size size;
    const Byte* bp = Cell_Bytes_Limit_At(&size, ARG(DATA), &limit);

    Option(SymId) envelope;
    if (not Bool_ARG(ENVELOPE))
        envelope = SYM_0;
    else {
        envelope = Cell_Word_Id(ARG(ENVELOPE));
        switch (envelope) {
          case SYM_ZLIB:
          case SYM_GZIP:
            break;

          default:
            assert(!"Bug in typechecking of envelope parameter");
        }
    }

    Size compressed_size;
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
//  "Decompresses DEFLATE-d data: https://en.wikipedia.org/wiki/DEFLATE"
//
//      return: [blob!]
//      data [blob! handle!]
//      :part "Length of compressed data (must match end marker)"
//          [integer! blob!]
//      :max "Error out if result is larger than this"
//          [integer!]
//      :envelope "ZLIB, GZIP, or DETECT (http://stackoverflow.com/a/9213826)"
//          [~(zlib gzip detect)~]
//  ]
//
DECLARE_NATIVE(INFLATE)
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
    if (Bool_ARG(MAX)) {
        max = Int32s(ARG(MAX), 1);
        if (max < 0)
            return PANIC(PARAM(MAX));
    }
    else
        max = -1;

    const Byte* data;
    Size size;
    if (Is_Blob(ARG(DATA))) {
        size = Part_Len_May_Modify_Index(ARG(DATA), ARG(PART));
        data = Cell_Blob_At(ARG(DATA));  // after (in case index modified)
    }
    else {
        size = Cell_Handle_Len(ARG(DATA));
        data = Cell_Handle_Pointer(Byte, ARG(DATA));
    }

    Option(SymId) envelope;
    if (not Bool_ARG(ENVELOPE))
        envelope = SYM_0;
    else {
        envelope = Cell_Word_Id(ARG(ENVELOPE));
        switch (envelope) {
          case SYM_ZLIB:
          case SYM_GZIP:
          case SYM_DETECT:
            break;

          default:
            assert(!"Bug in typechecking of envelope parameter");
        }
    }

    Size decompressed_size;
    void *decompressed = Decompress_Alloc_Core(
        &decompressed_size,
        data,
        size,
        max,
        envelope
    );

    return rebRepossess(decompressed, decompressed_size);
}
