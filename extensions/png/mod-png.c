//
//  File: %mod-lodepng.c
//  Summary: "Native Functions for cryptography"
//  Section: Extension
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
// R3-Alpha had some PNG decoding in a file called %u-png.c.  That decoder
// appeared to be original code from Rebol Technologies, as there are no
// comments saying otherwise.  Saphirion apparently hit bugs in the encoding
// that file implemented, but rather than try and figure out how to fix it
// they just included LodePNG--and adapted it for use in encoding only:
//
// http://lodev.org/lodepng/
//
// LodePNG is an encoder/decoder that is also a single source file and header
// file...but has some community of users and receives bugfixes.  So for
// simplicity, Ren-C went ahead and removed %u-png.c to use LodePNG for
// decoding and PNG file identification as well.
//
// Note: LodePNG is known to be slower than the more heavyweight "libpng"
// library, and does not support the progressive/streaming decoding used by
// web browsers.  For this reason, the extension is called "lodepng", to make
// room for more sophisticated PNG decoders in the future.
//

#include "lodepng.h"

#include "sys-core.h"

#include "tmp-mod-png.h"


//=//// CUSTOM SERIES-BACKED MEMORY ALLOCATOR /////////////////////////////=//
//
// LodePNG allows for a custom allocator.  %lodepng.h contains prototypes for
// these 3 functions, and expects them to be defined somewhere if you
// `#define LODEPNG_NO_COMPILE_ALLOCATORS` (set in %lodepng/make-spec.reb)
//
// Use rebMalloc(), because the memory can be later rebRepossess()'d into a
// Rebol BINARY! value without making a new buffer and copying.
//
//=////////////////////////////////////////////////////////////////////////=//

void* lodepng_malloc(size_t size)
  { return rebMalloc(size); }

void* lodepng_realloc(void* ptr, size_t new_size)
  { return rebRealloc(ptr, new_size); }

void lodepng_free(void* ptr)
  { rebFree(ptr); }


//=//// HOOKS TO REUSE REBOL'S ZLIB ///////////////////////////////////////=//
//
// By default, LodePNG will build its own copy of zlib functions for compress
// and decompress.  However, Rebol already has zlib built in.  So we ask
// LodePNG not to compile its own copy, and pass function pointers to do
// the compression and decompression in via the LodePNGState.
//
// Hence when lodepng.c is compiled, we `#define LODEPNG_NO_COMPILE_ZLIB`
// (set in %lodepng/make-spec.reb)
//
//=////////////////////////////////////////////////////////////////////////=//

static unsigned rebol_zlib_decompress(
    unsigned char **out,
    size_t *outsize,
    const unsigned char *in,
    size_t insize,
    const LodePNGDecompressSettings *settings
){
    // as far as I can tell, the logic of LodePNG is to preallocate a buffer
    // and so out and outsize are already set up.  This is due to some
    // knowledge it has about the scanlines.  But it's passed in as "out"
    // pointer parameters in case you update it (?)
    //
    // Rebol's decompression was not written for the caller to provide
    // a buffer, though COMPRESS/INTO or DECOMPRESS/INTO would be useful.
    // So consider it.  But for now, free the buffer and let the logic of
    // zlib always make its own.
    //
    rebFree(*out);

    assert(5 == *cast(const int*, settings->custom_context)); // just testing
    UNUSED(settings->custom_context);

    // PNG uses "zlib envelope" w/ADLER32 checksum, hence "Zinflate"
    //
    const REBINT max = -1; // size unknown, inflation will need to guess
    size_t out_len;
    *out = cast(unsigned char*, rebZinflateAlloc(&out_len, in, insize, max));
    *outsize = out_len;

    return 0;
}

static unsigned rebol_zlib_compress(
    unsigned char **out,
    size_t *outsize,
    const unsigned char *in,
    size_t insize,
    const LodePNGCompressSettings *settings
){
    lodepng_free(*out); // see remarks in decompress, and about COMPRESS/INTO

    assert(5 == *cast(const int*, settings->custom_context)); // just testing
    UNUSED(settings->custom_context);

    // PNG uses "zlib envelope" w/ADLER32 checksum, hence "Zdeflate"
    //
    *out = cast(unsigned char*, rebZdeflateAlloc(outsize, in, insize));

    return 0;
}


//
//  identify-png?: native [
//
//  {Codec for identifying BINARY! data for a PNG}
//
//      return: [logic!]
//      data [binary!]
//  ]
//
REBNATIVE(identify_png_q)
{
    PNG_INCLUDE_PARAMS_OF_IDENTIFY_PNG_Q;

    LodePNGState state;
    lodepng_state_init(&state);

    // use the zlib already built into Rebol for DECOMPRESS, inflate()
    //
    state.decoder.zlibsettings.custom_zlib = rebol_zlib_decompress;

    // this is how to pass an arbitrary void* that custom zlib can access
    // (so one could put decompression settings or state in there)
    //
    int arg = 5;
    state.decoder.zlibsettings.custom_context = &arg;

    REBSIZ size;
    const REBYTE *data = VAL_BINARY_SIZE_AT(&size, ARG(data));

    unsigned width;
    unsigned height;
    unsigned error = lodepng_inspect(
        &width,
        &height,
        &state,
        data,  // PNG data
        size  // PNG data length
    );

    // state contains extra information about the PNG such as text chunks
    //
    lodepng_state_cleanup(&state);

    if (error != 0)
        return Init_False(D_OUT);

    // !!! Should codec identifiers return any optional information they just
    // happen to get?  Instead of passing NULL for the addresses of the width
    // and the height, this could incidentally get that information back
    // to return it.  Then any non-FALSE result could be "identified" while
    // still being potentially more informative about what was found out.
    //
    return Init_True(D_OUT);
}


//
//  decode-png: native [
//
//  {Codec for decoding BINARY! data for a PNG}
//
//      return: [image!]
//      data [binary!]
//  ]
//
REBNATIVE(decode_png)
{
    PNG_INCLUDE_PARAMS_OF_DECODE_PNG;

    LodePNGState state;
    lodepng_state_init(&state);

    // use the zlib already built into Rebol for DECOMPRESS, inflate()
    //
    state.decoder.zlibsettings.custom_zlib = rebol_zlib_decompress;

    // this is how to pass an arbitrary void* that custom zlib can access
    // (so one could put decompression settings or state in there)
    //
    int arg = 5;
    state.decoder.zlibsettings.custom_context = &arg;

    // Even if the input PNG doesn't have alpha or color, ask for conversion
    // to RGBA.
    //
    state.decoder.color_convert = 1;
    state.info_png.color.colortype = LCT_RGBA;
    state.info_png.color.bitdepth = 8;

    REBSIZ size;
    const REBYTE *data = VAL_BINARY_SIZE_AT(&size, ARG(data));

    unsigned char* image_bytes;
    unsigned w;
    unsigned h;
    unsigned error = lodepng_decode(
        &image_bytes,
        &w,
        &h,
        &state,
        data,  // PNG data
        size  // PNG data length
    );

    // `state` can contain potentially interesting information, such as
    // metadata (key="Software" value="REBOL", for instance).  Currently this
    // is just thrown away, but it might be interesting to have access to.
    // Because Rebol_Malloc() was used to make the strings, they could easily
    // be Rebserize()'d and put in an object.
    //
    lodepng_state_cleanup(&state);

    if (error != 0)
        fail (lodepng_error_text(error));

    // Note LodePNG cannot decode into an existing buffer, though it has been
    // requested:
    //
    // https://github.com/lvandeve/lodepng/issues/17
    //

    REBVAL *binary = rebRepossess(image_bytes, (w * h) * 4);

    return rebValue(
        "make image! compose [",
            "(make pair! [", rebI(w), rebI(h), "])",
            rebR(binary),
        "]"
    );
}


//
//  encode-png: native [
//
//  {Codec for encoding a PNG image}
//
//      return: [binary!]
//      image [image!]
// ]
//
REBNATIVE(encode_png)
//
// !!! Semantics for IMAGE! being a "series" with a "position" were extremely
// dodgy in Rebol2/R3-Alpha (and remain so in things like Red today).  Saving
// is no exception, Red seems to throw out the concept altogether there:
//
//     red>> i: make image! 2x2
//     red>> i/1: 1.2.3
//     red>> i
//     == make image! [2x2 #{010203FFFFFFFFFFFFFFFFFF}]
//
//     red>> i: tail i
//     == make image! [2x2 #{}]
//
//     red>> save %test.png i
//
//     red>> load %test.png
//     == make image! [2x2 #{010203FFFFFFFFFFFFFFFFFF}]
//
// R3-Alpha gives a similar answer (unused pixels are 00, RGB reverse order).
// Rebol2 gives back `make image! [2x2 #{}]`, losing the data.
//
// We write the head position here--for lack of a better answer.
{
    PNG_INCLUDE_PARAMS_OF_ENCODE_PNG;

    REBVAL *image = ARG(image);
    REBVAL *head = rebValue("head", image);  // ^-- see notes above on position
    Move_Cell(image, head);
    rebRelease(head);

    // Historically, Rebol would write (key="Software" value="REBOL") into
    // image metadata.  Is that interesting?  If so, the state has fields for
    // this...assuming the encoder pays attention to them (the decoder does).
    //
    LodePNGState state;
    lodepng_state_init(&state);

    // use the zlib already built into Rebol for DECOMPRESS, deflate()
    //
    state.encoder.zlibsettings.custom_zlib = rebol_zlib_compress;

    // this is how to pass an arbitrary void* that custom zlib can access
    // (so one could put dompression settings or state in there)
    //
    int arg = 5;
    state.encoder.zlibsettings.custom_context = &arg;

    // input format
    //
    state.info_raw.colortype = LCT_RGBA;
    state.info_raw.bitdepth = 8;

    // output format - could support more options, like LCT_RGB to avoid
    // writing transparency, or grayscale, etc.
    //
    state.info_png.color.colortype = LCT_RGBA;
    state.info_png.color.bitdepth = 8;

    // !!! "disable autopilot" (what is the significance of this?  it might
    // have to be 1 if using an output format different from the input...)
    //
    state.encoder.auto_convert = 0;

    REBVAL *size = rebValue("pick", image, "'size");
    REBLEN width = rebUnboxInteger("pick", size, "'x");
    REBLEN height = rebUnboxInteger("pick", size, "'y");
    rebRelease(size);

    size_t binsize;
    REBYTE *image_bytes = rebBytes(&binsize, "bytes of", image);

    size_t encoded_size;
    REBYTE *encoded_bytes = NULL;
    unsigned error = lodepng_encode(
        &encoded_bytes,
        &encoded_size,
        image_bytes,
        width,
        height,
        &state
    );
    lodepng_state_cleanup(&state);

    rebFree(image_bytes);

    if (error != 0)
        fail (lodepng_error_text(error));

    // Because LodePNG was hooked with a custom zlib_malloc, it built upon
    // rebMalloc()...which backs its allocations with a series.  This means
    // the encoded buffer can be taken back as a BINARY! without making a
    // new series, see rebMalloc()/rebRepossess() for details.
    //
    return rebRepossess(encoded_bytes, encoded_size);
}
