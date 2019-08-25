//
//  File: %mod-gif.c
//  Summary: "GIF image format conversion"
//  Section: Extension
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
//=////////////////////////////////////////////////////////////////////////=//
//
// This is an optional part of R3. This file can be replaced by
// library function calls into an updated implementation.
//

#include "sys-core.h"

#include "tmp-mod-gif.h"

#define MAX_STACK_SIZE  4096
#define NULL_CODE       (-1)
#define BitSet(byte,bit)  (((byte) & (bit)) == (bit))
#define LSBFirstOrder(x,y)  (((y) << 8) | (x))

static int32_t   interlace_rate[4] = { 8, 8, 4, 2 },
                interlace_start[4] = { 0, 4, 2, 1 };


#ifdef COMP_IMAGES
// Because graphics.c is not included, we must have a copy here.
void Chrom_Key_Alpha(REBVAL *v,uint32_t col,int32_t blitmode) {
    bool found=false;
    int i;
    uint32_t *p;

    p=(uint32_t *)VAL_IMAGE_HEAD(v);
    i=VAL_IMAGE_WIDTH(v)*VAL_IMAGE_HEIGHT(v);
    switch(blitmode) {
        case BLIT_MODE_COLOR:
            for(;i>0;i--,p++) {
                if(*p==col) {
                    found=true;
                    *p=col|0xff000000;
                }
            }
        case BLIT_MODE_LUMA:
            for(;i>0;i--,p++) {
                if(BRIGHT(((REBRGB *)p))<=col) {
                    found=true;
                    *p|=0xff000000;
                }
            }
            break;
    }
    if(found)
        VAL_IMAGE_TRANSP(v)=VITT_ALPHA;
}
#endif

//
//  Decode_LZW: C
//
// Perform LZW decompression.
//
void Decode_LZW(REBYTE *data, REBYTE **cpp, REBYTE *colortab, int32_t w, int32_t h, bool interlaced)
{
    REBYTE  *cp = *cpp;
    REBYTE  *rp;
    int32_t  available, clear, code_mask, code_size, end_of_info, in_code;
    int32_t  old_code, bits, code, count, x, y, data_size, row, i;
    REBYTE *dp;
    uint32_t datum;
    short   *prefix;
    REBYTE  first, *pixel_stack, *suffix, *top_stack;

    suffix = rebAllocN(REBYTE,
        MAX_STACK_SIZE * (sizeof(REBYTE) + sizeof(REBYTE) + sizeof(short))
    );
    pixel_stack = suffix + MAX_STACK_SIZE;
    prefix = (short *)(pixel_stack + MAX_STACK_SIZE);

    data_size = *cp++;
    clear = 1 << data_size;
    end_of_info = clear + 1;
    available = clear + 2;
    old_code = NULL_CODE;
    code_size = data_size + 1;
    code_mask = (1 << code_size) - 1;

    for (code=0; code<clear; code++) {
        prefix[code] = 0;
        suffix[code] = code;
    }

    datum = 0;
    bits = 0;
    count = 0;
    first = 0;
    row = i = 0;
    top_stack = pixel_stack;
    dp = data;
    for (y=0; y<h; y++) {
        for (x=0; x<w;) {
            // if the stack is empty
            if (top_stack == pixel_stack) {
                // if we don't have enough bits
                if (bits < code_size) {
                    // if we run out of bytes in the packet
                    if (count == 0) {
                        // get size of next packet
                        count = *cp++;
                        // if 0, end of image
                        if (count == 0)
                            break;
                    }
                    // add bits from next byte and adjust counters
                    datum += *cp++ << bits;
                    bits += 8;
                    count--;
                    continue;
                }
                // isolate the code bits and adjust the temps
                code = datum & code_mask;
                datum >>= code_size;
                bits -= code_size;

                // sanity check
                if (code > available || code == end_of_info)
                    break;
                // time to reset the tables
                if (code == clear) {
                    code_size = data_size + 1;
                    code_mask = (1 << code_size) - 1;
                    available = clear + 2;
                    old_code = NULL_CODE;
                    continue;
                }
                // if we are the first code, just stack it
                if (old_code == NULL_CODE) {
                    *top_stack++ = suffix[code];
                    old_code = code;
                    first = code;
                    continue;
                }
                in_code = code;
                if (code == available) {
                    *top_stack++ = first;
                    code = old_code;
                }
                while (code > clear) {
                    *top_stack++ = suffix[code];
                    code = prefix[code];
                }
                first = suffix[code];

                // add a new string to the table
                if (available >= MAX_STACK_SIZE)
                    break;
                *top_stack++ = first;
                prefix[available] = old_code;
                suffix[available++] = first;
                if ((available & code_mask) == 0 && available < MAX_STACK_SIZE) {
                    code_size++;
                    code_mask += available;
                }
                old_code = in_code;
            }
            top_stack--;
            rp = colortab + 3 * *top_stack;
            *dp++ = rp[0]; // red
            *dp++ = rp[1]; // green
            *dp++ = rp[2]; // blue
            *dp++ = 0xff; // alpha
            x++;
        }
        if (interlaced) {
            row += interlace_rate[i];
            if (row >= h) {
                row = interlace_start[++i];
            }
            dp = data + ((row * w) * 4);
        }
    }
    *cpp = cp + count + 1;

    rebFree(suffix);
}


static bool Has_Valid_GIF_Header(REBYTE *data, uint32_t len) {
    if (len < 5)
        return false;

    if (strncmp(cast(char*, data), "GIF87", 5) == 0)
        return true;

    if (strncmp(cast(char*, data), "GIF89", 5) == 0)
        return true;

    return false;
}


//
//  identify-gif?: native [
//
//  {Codec for identifying BINARY! data for a GIF}
//
//      return: [logic!]
//      data [binary!]
//  ]
//
REBNATIVE(identify_gif_q)
{
    GIF_INCLUDE_PARAMS_OF_IDENTIFY_GIF_Q;

    REBYTE *data = VAL_BIN_AT(ARG(data));
    uint32_t len = VAL_LEN_AT(ARG(data));

    // Assume signature matching is good enough (will get a fail() on
    // decode if it's a false positive).
    //
    return Init_Logic(D_OUT, Has_Valid_GIF_Header(data, len));
}


//
//  decode-gif: native [
//
//  {Codec for decoding BINARY! data for a GIF}
//
//      return: [image! block!]
//          {Single image or BLOCK! of images if multiple frames (animated)}
//      data [binary!]
//  ]
//
REBNATIVE(decode_gif)
{
    GIF_INCLUDE_PARAMS_OF_DECODE_GIF;

    REBYTE *data = VAL_BIN_AT(ARG(data));
    uint32_t len = VAL_LEN_AT(ARG(data));

    if (not Has_Valid_GIF_Header(data, len))
        fail (Error_Bad_Media_Raw());

    int32_t  w, h;
    int32_t  transparency_index;
    REBYTE  c, *global_colormap, *colormap;
    uint32_t  global_colors, local_colormap;
    uint32_t  colors;
    bool  interlaced;

    REBYTE *cp = data;
    REBYTE *end = data + len;

    global_colors = 0;
    global_colormap = (unsigned char *) NULL;
    if (cp[10] & 0x80) {
        // Read global colormap.
        global_colors = 1 << ((cp[10] & 0x07) + 1);
        global_colormap = cp + 13;
        cp += global_colors * 3;
    }
    cp += 13;
    transparency_index = -1;

    REBVAL *frames = rebValue("copy []", rebEND);

    for (;;) {
        if (cp >= end) break;
        c = *cp++;

        if (c == ';')
            break;  // terminator

        if (c == '!') {
            // GIF Extension block.
            c = *cp++;
            switch (c) {
            case 0xf9:
                // Transparency extension block.
                while (cp[0] != 0 && cp[5] != 0)
                    cp += 5;
                if ((cp[1] & 0x01) == 1)
                    transparency_index = cp[4];
                cp += cp[0] + 1 + 1;
                break;

            default:
                while (cp[0] != 0)
                    cp += cp[0] + 1;
                cp++;
                break;
            }
        }

        if (c != ',') continue;

        interlaced = did (cp[8] & 0x40);
        local_colormap = cp[8] & 0x80;

        w = LSBFirstOrder(cp[4],cp[5]);
        h = LSBFirstOrder(cp[6],cp[7]);
        // if(w * h * 4 > VAL_SERIES_LEN(img))
        //          h = 4 * VAL_SERIES_LEN(img) / w;

        // Initialize colormap.
        if (local_colormap) {
            colors = 1 << ((cp[8] & 0x07) + 1);
            colormap = cp + 9;
            cp += 3 * colors;
        }
        else {
            colors = global_colors;
            colormap = global_colormap;
        }
        cp += 9;

        REBYTE *dp = rebAllocN(REBYTE, (w * h) * 4);  // RGBA pixels, 4 bytes

        Decode_LZW(dp, &cp, colormap, w, h, interlaced);

        if(transparency_index >= 0) {
            REBYTE *p=colormap+3*transparency_index;
            UNUSED(p);
            ///Chroma_Key_Alpha(Temp_Value, (uint32_t)(p[2]|(p[1]<<8)|(p[0]<<16)), BLIT_MODE_COLOR);
        }

        REBVAL *binary = rebRepossess(dp, (w * h) * 4);

        rebElide(
            "append/only", frames, "make image! compose [",
                "(to pair! [", rebI(w), rebI(h), "])",
                binary,
            "]",
        rebEND);

        rebRelease(binary);
    }

    // If 0 images, raise an error
    // If 1 image, return as a single value
    // If multiple images, return in a BLOCK!
    //
    // !!! Should formats that can act as containers always return a BLOCK!?
    //
    REBVAL *result = rebValue("case [",
        "empty?", frames, "[FAIL {No frames found in GIF}]",
        "1 = length of", frames, "[first", frames, "]",
        "default [", frames, "]",
    "]", rebEND);

    rebRelease(frames);

    return result;
}
