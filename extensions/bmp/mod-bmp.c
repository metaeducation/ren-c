//
//  File: %mod-bmp.c
//  Summary: "conversion to and from BMP graphics format"
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
// This is an optional part of R3. This file can be replaced by
// library function calls into an updated implementation.
//

#include "sys-core.h"

#include "tmp-mod-bmp.h"

//**********************************************************************

#define WADJUST(x) (((x * 3L + 3) / 4) * 4)

typedef unsigned char   BYTE;
typedef unsigned short  WORD;
typedef unsigned int    DWORD;
typedef int             LONG;

typedef struct tagBITMAP
{
    int     bmType;
    int     bmWidth;
    int     bmHeight;
    int     bmWidthBytes;
    BYTE    bmPlanes;
    BYTE    bmBitsPixel;
    void    *bmBits;
} BITMAP;
typedef BITMAP *PBITMAP;
typedef BITMAP *NPBITMAP;
typedef BITMAP *LPBITMAP;

/* Bitmap Header structures */
typedef struct tagRGBTRIPLE
{
    BYTE    rgbtBlue;
    BYTE    rgbtGreen;
    BYTE    rgbtRed;
} RGBTRIPLE;
typedef RGBTRIPLE *LPRGBTRIPLE;

typedef struct tagRGBQUAD
{
    BYTE    rgbBlue;
    BYTE    rgbGreen;
    BYTE    rgbRed;
    BYTE    rgbReserved;
} RGBQUAD;
typedef RGBQUAD *LPRGBQUAD;

/* structures for defining DIBs */
typedef struct tagBITMAPCOREHEADER
{
    DWORD   bcSize;
    short   bcWidth;
    short   bcHeight;
    WORD    bcPlanes;
    WORD    bcBitCount;
} BITMAPCOREHEADER;
typedef BITMAPCOREHEADER*      PBITMAPCOREHEADER;
typedef BITMAPCOREHEADER *LPBITMAPCOREHEADER;

const char *mapBITMAPCOREHEADER = "lssss";

typedef struct tagBITMAPINFOHEADER
{
    DWORD   biSize;
    LONG    biWidth;
    LONG    biHeight;
    WORD    biPlanes;
    WORD    biBitCount;
    DWORD   biCompression;
    DWORD   biSizeImage;
    LONG    biXPelsPerMeter;
    LONG    biYPelsPerMeter;
    DWORD   biClrUsed;
    DWORD   biClrImportant;
} BITMAPINFOHEADER;

const char *mapBITMAPINFOHEADER = "lllssllllll";

typedef BITMAPINFOHEADER*      PBITMAPINFOHEADER;
typedef BITMAPINFOHEADER *LPBITMAPINFOHEADER;

/* constants for the biCompression field */
#define BI_RGB      0L
#define BI_RLE8     1L
#define BI_RLE4     2L

typedef struct tagBITMAPINFO
{
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD          bmiColors[1];
} BITMAPINFO;
typedef BITMAPINFO*     PBITMAPINFO;
typedef BITMAPINFO *LPBITMAPINFO;

typedef struct tagBITMAPCOREINFO
{
    BITMAPCOREHEADER bmciHeader;
    RGBTRIPLE        bmciColors[1];
} BITMAPCOREINFO;
typedef BITMAPCOREINFO*      PBITMAPCOREINFO;
typedef BITMAPCOREINFO *LPBITMAPCOREINFO;

typedef struct tagBITMAPFILEHEADER
{
    char    bfType[2];
    DWORD   bfSize;
    WORD    bfReserved1;
    WORD    bfReserved2;
    DWORD   bfOffBits;
} BITMAPFILEHEADER;
typedef BITMAPFILEHEADER*      PBITMAPFILEHEADER;
typedef BITMAPFILEHEADER *LPBITMAPFILEHEADER;

const char *mapBITMAPFILEHEADER = "bblssl";

typedef RGBQUAD *RGBQUADPTR;

//**********************************************************************

static bool longaligned(void) {
    static char filldata[] = {0,0,1,1,1,1};
    struct {
        unsigned short a;
        unsigned int b;
    } a;
    memset(&a, '\0', sizeof(a));
    memcpy(&a, filldata, 6);
    if (a.b != 0x01010101)
        return true;
    return false;
}

void Map_Bytes(void *dstp, const REBYTE **srcp, const char *map) {
    const REBYTE *src = *srcp;
    REBYTE *dst = cast(REBYTE*, dstp);
    char c;
#ifdef ENDIAN_LITTLE
    while ((c = *map++) != 0) {
        switch(c) {
        case 'b':
            *dst++ = *src++;
            break;

        case 's':
            *((short *)dst) = *((const short *)src);
            dst += sizeof(short);
            src += 2;
            break;

        case 'l':
            if (longaligned()) {
                while ((cast(uintptr_t, dst) & 3) != 0)
                    dst++;
            }
            *((uint32_t *)dst) = *((const uint32_t *)src);
            dst += sizeof(uint32_t);
            src += 4;
            break;
        }
    }
#else
    while ((c = *map++) != 0) {
        switch(c) {
        case 'b':
            *dst++ = *src++;
            break;

        case 's':
            *((short *)dst) = src[0]|(src[1]<<8);
            dst += sizeof(short);
            src += 2;
            break;

        case 'l':
            if (longaligned()) {
                while (((unsigned long)dst)&3)
                    dst++;
            }
            *((uint32_t *)dst) = src[0]|(src[1]<<8)|
                    (src[2]<<16)|(src[3]<<24);
            dst += sizeof(uint32_t);
            src += 4;
            break;
        }
    }
#endif
    *srcp = src;
}

void Unmap_Bytes(void *srcp, REBYTE **dstp, const char *map) {
    REBYTE *src = cast(REBYTE*, srcp);
    REBYTE *dst = *dstp;
    char c;
#ifdef ENDIAN_LITTLE
    while ((c = *map++) != 0) {
        switch(c) {
        case 'b':
            *dst++ = *src++;
            break;

        case 's':
            *((short *)dst) = *((short *)src);
            src += sizeof(short);
            dst += 2;
            break;

        case 'l':
            if (longaligned()) {
                while((cast(uintptr_t, src) & 3) != 0)
                    src++;
            }
            *((uint32_t *)dst) = *((uint32_t *)src);
            src += sizeof(uint32_t);
            dst += 4;
            break;
        }
    }
#else
    while ((c = *map++) != 0) {
        switch(c) {
        case 'b':
            *dst++ = *src++;
            break;

        case 's':
            *((short *)dst) = src[0]|(src[1]<<8);
            src += sizeof(short);
            dst += 2;
            break;

        case 'l':
            if (longaligned()) {
                while (((unsigned long)src)&3)
                    src++;
            }
            *((uint32_t *)dst) = src[0]|(src[1]<<8)|
                    (src[2]<<16)|(src[3]<<24);
            src += sizeof(uint32_t);
            dst += 4;
            break;
        }
    }
#endif
    *dstp = dst;
}


static bool Has_Valid_BITMAPFILEHEADER(const REBYTE *data, uint32_t len) {
    if (len < sizeof(BITMAPFILEHEADER))
        return false;

    BITMAPFILEHEADER bmfh;
    Map_Bytes(&bmfh, &data, mapBITMAPFILEHEADER);

    if (bmfh.bfType[0] != 'B' || bmfh.bfType[1] != 'M')
        return false;

    return true;
}


//
//  identify-bmp?: native [
//
//  {Codec for identifying BINARY! data for a BMP}
//
//      return: [logic!]
//      data [binary!]
//  ]
//
REBNATIVE(identify_bmp_q)
{
    BMP_INCLUDE_PARAMS_OF_IDENTIFY_BMP_Q;

    REBSIZ size;
    const REBYTE *data = VAL_BINARY_SIZE_AT(&size, ARG(data));

    // Assume signature matching is good enough (will get a fail() on
    // decode if it's a false positive).
    //
    return Init_Logic(OUT, Has_Valid_BITMAPFILEHEADER(data, size));
}


//
//  decode-bmp: native [
//
//  {Codec for decoding BINARY! data for a BMP}
//
//      return: [image!]
//      data [binary!]
//  ]
//
REBNATIVE(decode_bmp)
{
    BMP_INCLUDE_PARAMS_OF_DECODE_BMP;

    REBSIZ size;
    const REBYTE *data = VAL_BINARY_SIZE_AT(&size, ARG(data));

    if (not Has_Valid_BITMAPFILEHEADER(data, size))
        fail (Error_Bad_Media_Raw());

    int32_t              i, j, x, y, c;
    int32_t              colors, compression, bitcount;
    int32_t              w, h;
    BITMAPINFOHEADER    bmih;
    BITMAPCOREHEADER    bmch;
    RGBQUADPTR          color;
    RGBQUADPTR          ctab = 0;

    const REBYTE *cp = data;

    // !!! It strangely appears that passing &data instead of &cp to this
    // Map_Bytes call causes bugs below.  Not clear why that would be.
    //
    BITMAPFILEHEADER bmfh;
    Map_Bytes(&bmfh, &cp, mapBITMAPFILEHEADER); // length already checked

    const REBYTE *tp = cp;
    Map_Bytes(&bmih, &cp, mapBITMAPINFOHEADER);
    if (bmih.biSize < sizeof(BITMAPINFOHEADER)) {
        cp = tp;
        Map_Bytes(&bmch, &cp, mapBITMAPCOREHEADER);

        w = bmch.bcWidth;
        h = bmch.bcHeight;
        compression = 0;
        bitcount = bmch.bcBitCount;

        if (bmch.bcBitCount < 24)
            colors = 1 << bmch.bcBitCount;
        else
            colors = 0;

        if (colors) {
            ctab = TRY_ALLOC_N(RGBQUAD, colors);
            for (i = 0; i<colors; i++) {
                ctab[i].rgbBlue = *cp++;
                ctab[i].rgbGreen = *cp++;
                ctab[i].rgbRed = *cp++;
                ctab[i].rgbReserved = 0;
            }
        }
    }
    else {
        w = bmih.biWidth;
        h = bmih.biHeight;
        compression = bmih.biCompression;
        bitcount = bmih.biBitCount;

        if (bmih.biClrUsed == 0 && bmih.biBitCount < 24)
            colors = 1 << bmih.biBitCount;
        else
            colors = bmih.biClrUsed;

        if (colors) {
            ctab = TRY_ALLOC_N(RGBQUAD, colors);
            memcpy(ctab, cp, colors * sizeof(RGBQUAD));
            cp += colors * sizeof(RGBQUAD);
        }
    }

    if (bmfh.bfOffBits != cast(DWORD, cp - data))
        cp = data + bmfh.bfOffBits;

    REBYTE *image_bytes = rebAllocN(REBYTE, (w * h) * 4);  // RGBA is 4 bytes

    REBYTE *dp = image_bytes;

    dp += (w * h - w) * 4;

    c = 0xDECAFBAD; // should be overwritten, but avoid uninitialized warning
    x = 0xDECAFBAD; // should be overwritten, but avoid uninitialized warning

    for (y = 0; y<h; y++) {
        switch(compression) {
        case BI_RGB:
            switch(bitcount) {
            case 1:
                x = 0;
                for (i = 0; i<w; i++) {
                    if (x == 0) {
                        x = 0x80;
                        c = *cp++ & 0xff;
                    }
                    color = &ctab[(c&x) != 0];
                    *dp++ = color->rgbRed;
                    *dp++ = color->rgbGreen;
                    *dp++ = color->rgbBlue;
                    *dp++ = 0xff; // opaque alpha
                    x >>= 1;
                }
                i = (w+7) / 8;
                break;

            case 4:
                for (i = 0; i<w; i++) {
                    if ((i&1) == 0) {
                        c = *cp++ & 0xff;
                        x = c >> 4;
                    }
                    else
                        x = c & 0xf;
                    if (x > colors) {
                        goto bad_table_error;
                    }
                    color = &ctab[x];
                    *dp++ = color->rgbRed;
                    *dp++ = color->rgbGreen;
                    *dp++ = color->rgbBlue;
                    *dp++ = 0xff; // opaque alpha
                }
                i = (w+1) / 2;
                break;

            case 8:
                for (i = 0; i<w; i++) {
                    c = *cp++ & 0xff;
                    if (c > colors) {
                        goto bad_table_error;
                    }
                    color = &ctab[c];
                    *dp++ = color->rgbRed;
                    *dp++ = color->rgbGreen;
                    *dp++ = color->rgbBlue;
                    *dp++ = 0xff; // opaque alpha
                }
                break;

            case 24:
                for (i = 0; i<w; i++) {
                    *dp++ = cp[2]; // red
                    *dp++ = cp[1]; // green
                    *dp++ = cp[0]; // blue
                    *dp++ = 0xff; // opaque alpha
                    cp += 3;
                }
                i = w * 3;
                break;

            default:
                goto bit_len_error;
            }
            while (i++ % 4)
                cp++;
            break;

        case BI_RLE4:
            i = 0;
            for (;;) {
                c = *cp++ & 0xff;

                if (c == 0) {
                    c = *cp++ & 0xff;
                    if (c == 0 || c == 1)
                        break;
                    if (c == 2) {
                        goto bad_table_error;
                    }
                    for (j = 0; j<c; j++) {
                        if (i == w)
                            goto bad_table_error;
                        if ((j&1) == 0) {
                            x = *cp++ & 0xff;
                            color = &ctab[x>>4];
                        }
                        else
                            color = &ctab[x&0x0f];
                        *dp++ = color->rgbRed;
                        *dp++ = color->rgbGreen;
                        *dp++ = color->rgbBlue;
                        *dp++ = 0xff; // opaque alpha
                    }
                    j = (c+1) / 2;
                    while (j++%2)
                        cp++;
                }
                else {
                    x = *cp++ & 0xff;
                    for (j = 0; j<c; j++) {
                        if (i == w) {
                            goto bad_table_error;
                        }
                        if (j&1)
                            color = &ctab[x&0x0f];
                        else
                            color = &ctab[x>>4];
                        *dp++ = color->rgbRed;
                        *dp++ = color->rgbGreen;
                        *dp++ = color->rgbBlue;
                        *dp++ = 0xff; // opaque alpha
                    }
                }
            }
            break;

        case BI_RLE8:
            i = 0;
            for (;;) {
                c = *cp++ & 0xff;

                if (c == 0) {
                    c = *cp++ & 0xff;
                    if (c == 0 || c == 1)
                        break;
                    if (c == 2) {
                        goto bad_table_error;
                    }
                    for (j = 0; j<c; j++) {
                        x = *cp++ & 0xff;
                        color = &ctab[x];
                        *dp++ = color->rgbRed;
                        *dp++ = color->rgbGreen;
                        *dp++ = color->rgbBlue;
                        *dp++ = 0xff; // opaque alpha
                    }
                    while (j++ % 2)
                        cp++;
                }
                else {
                    x = *cp++ & 0xff;
                    for (j = 0; j<c; j++) {
                        color = &ctab[x];
                        *dp++ = color->rgbRed;
                        *dp++ = color->rgbGreen;
                        *dp++ = color->rgbBlue;
                        *dp++ = 0xff; // opaque alpha
                    }
                }
            }
            break;

        default:
            goto bad_encoding_error;
        }
        dp -= (2 * w) * 4;
    }

  blockscope {
    REBVAL *binary = rebRepossess(image_bytes, (w * h) * 4);

    return rebValue(
        "make image! compose [",
            "(make pair! [", rebI(w), rebI(h), "])",
            rebR(binary),
        "]"
    ); }

  bit_len_error:
  bad_encoding_error:
  bad_table_error:

    if (ctab)
        free(ctab);
    fail (Error_Bad_Media_Raw()); // better error?
}


//
//  encode-bmp: native [
//
//  {Codec for encoding a BMP image}
//
//      return: [binary!]
//      image [image!]
//  ]
//
REBNATIVE(encode_bmp)
{
    BMP_INCLUDE_PARAMS_OF_ENCODE_BMP;

    int32_t i, y;
    BITMAPFILEHEADER bmfh;
    BITMAPINFOHEADER bmih;

    REBVAL *size = rebValue("pick", ARG(image), "'size");
    int32_t w = rebUnboxInteger("pick", size, "'x");
    int32_t h = rebUnboxInteger("pick", size, "'y");
    rebRelease(size);

    size_t binsize;
    REBYTE *image_bytes = rebBytes(&binsize, "bytes of", ARG(image));
    assert(cast(int32_t, binsize) == w * h * 4);

    memset(&bmfh, 0, sizeof(bmfh));
    bmfh.bfType[0] = 'B';
    bmfh.bfType[1] = 'M';
    bmfh.bfSize = 14 + 40 + h * WADJUST(w);
    bmfh.bfOffBits = 14 + 40;

    REBYTE *bmp_bytes = rebAllocN(REBYTE, bmfh.bfSize);
    REBYTE *cp = bmp_bytes;
    Unmap_Bytes(&bmfh, &cp, mapBITMAPFILEHEADER);

    memset(&bmih, 0, sizeof(bmih));
    bmih.biSize = 40;
    bmih.biWidth = w;
    bmih.biHeight = h;
    bmih.biPlanes = 1;
    bmih.biBitCount = 24;
    bmih.biCompression = 0;
    bmih.biSizeImage = 0;
    bmih.biXPelsPerMeter = 0;
    bmih.biYPelsPerMeter = 0;
    bmih.biClrUsed = 0;
    bmih.biClrImportant = 0;
    Unmap_Bytes(&bmih, &cp, mapBITMAPINFOHEADER);

    REBYTE *dp = image_bytes + ((w * h - w) * 4);

    for (y = 0; y<h; y++) {
        for (i = 0; i<w; i++) {
            //
            // BMP files are written out in bytes as Blue, Green, Red.  `bgra`
            // was the only BMP subformat that supported transparency, so this
            // likely contributed to the popularity of this order.  However
            // dp[3] alpha component is ignored in this code.
            //
            cp[0] = dp[2]; // first is blue (rgba[2])
            cp[1] = dp[1]; // second is green (rgba[1])
            cp[2] = dp[0]; // third is red (rgba[0])
            // ignore dp[3] alpha
            cp += 3; // rgb
            dp += 4; // rgba
        }
        i = w * 3;
        while (i++ % 4)
            *cp++ = 0;
        dp -= (2 * w) * 4;
    }

    rebFree(image_bytes);

    REBVAL *binary = rebRepossess(bmp_bytes, bmfh.bfSize);
    return binary;
}
