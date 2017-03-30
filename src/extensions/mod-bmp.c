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
// This is an optional part of R3. This file can be replaced by
// library function calls into an updated implementation.
//

#include "sys-core.h"
#include "sys-ext.h"
#include "tmp-mod-bmp-first.h"

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

static REBOOL longaligned(void) {
    static char filldata[] = {0,0,1,1,1,1};
    struct {
        unsigned short a;
        unsigned int b;
    } a;
    memset(&a, '\0', sizeof(a));
    memcpy(&a, filldata, 6);
    if (a.b != 0x01010101) return TRUE;
    return FALSE;
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
                while(((REBUPT)dst)&3)
                    dst++;
            }
            *((REBCNT *)dst) = *((const REBCNT *)src);
            dst += sizeof(REBCNT);
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
            *((REBCNT *)dst) = src[0]|(src[1]<<8)|
                    (src[2]<<16)|(src[3]<<24);
            dst += sizeof(REBCNT);
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
                while(((REBUPT)src)&3)
                    src++;
            }
            *((REBCNT *)dst) = *((REBCNT *)src);
            src += sizeof(REBCNT);
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
            *((REBCNT *)dst) = src[0]|(src[1]<<8)|
                    (src[2]<<16)|(src[3]<<24);
            src += sizeof(REBCNT);
            dst += 4;
            break;
        }
    }
#endif
    *dstp = dst;
}


static REBOOL Has_Valid_BITMAPFILEHEADER(const REBYTE *data, REBCNT len) {
    if (len < sizeof(BITMAPFILEHEADER))
        return FALSE;

    BITMAPFILEHEADER bmfh;
    Map_Bytes(&bmfh, &data, mapBITMAPFILEHEADER);

    if (bmfh.bfType[0] != 'B' || bmfh.bfType[1] != 'M')
        return FALSE;

    return TRUE;
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
    INCLUDE_PARAMS_OF_IDENTIFY_BMP_Q;

    const REBYTE *data = VAL_BIN_AT(ARG(data));
    REBCNT len = VAL_LEN_AT(ARG(data));

    // Assume signature matching is good enough (will get a fail() on
    // decode if it's a false positive).
    //
    return R_FROM_BOOL(Has_Valid_BITMAPFILEHEADER(data, len));
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
    INCLUDE_PARAMS_OF_DECODE_BMP;

    const REBYTE *data = VAL_BIN_AT(ARG(data));
    REBCNT len = VAL_LEN_AT(ARG(data));

    if (NOT(Has_Valid_BITMAPFILEHEADER(data, len)))
        fail (Error_Bad_Media_Raw());

    REBINT              i, j, x, y, c;
    REBINT              colors, compression, bitcount;
    REBINT              w, h;
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
            ctab = ALLOC_N(RGBQUAD, colors);
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
            ctab = ALLOC_N(RGBQUAD, colors);
            memcpy(ctab, cp, colors * sizeof(RGBQUAD));
            cp += colors * sizeof(RGBQUAD);
        }
    }

    if (bmfh.bfOffBits != cast(DWORD, cp - data))
        cp = data + bmfh.bfOffBits;

    REBSER *ser = Make_Image(w, h, TRUE);

    REBCNT *dp = cast(REBCNT *, IMG_DATA(ser));
    
    dp += w * h - w;

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
                    *dp++ = TO_PIXEL_COLOR(color->rgbRed, color->rgbGreen, color->rgbBlue, 0xff);
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
                    *dp++ = TO_PIXEL_COLOR(color->rgbRed, color->rgbGreen, color->rgbBlue, 0xff);
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
                    *dp++ = TO_PIXEL_COLOR(color->rgbRed, color->rgbGreen, color->rgbBlue, 0xff);
                }
                break;

            case 24:
                for (i = 0; i<w; i++) {
                    *dp++ = TO_PIXEL_COLOR(cp[2], cp[1], cp[0], 0xff);
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
                        *dp++ = TO_PIXEL_COLOR(color->rgbRed, color->rgbGreen, color->rgbBlue, 0xff);
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
                        *dp++ = TO_PIXEL_COLOR(color->rgbRed, color->rgbGreen, color->rgbBlue, 0xff);
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
                        *dp++ = TO_PIXEL_COLOR(color->rgbRed, color->rgbGreen, color->rgbBlue, 0xff);
                    }
                    while (j++ % 2)
                        cp++;
                }
                else {
                    x = *cp++ & 0xff;
                    for (j = 0; j<c; j++) {
                        color = &ctab[x];
                        *dp++ = TO_PIXEL_COLOR(color->rgbRed, color->rgbGreen, color->rgbBlue, 0xff);
                    }
                }
            }
            break;

        default:
            goto bad_encoding_error;
        }
        dp -= 2 * w;
    }

    Init_Image(D_OUT, ser);
    return R_OUT;

bit_len_error:
bad_encoding_error:
bad_table_error:
    if (ctab) free(ctab);
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
    INCLUDE_PARAMS_OF_ENCODE_BMP;

    REBINT i, y;
    REBYTE *cp, *v;
    REBCNT *dp;
    BITMAPFILEHEADER bmfh;
    BITMAPINFOHEADER bmih;

    REBINT w = VAL_IMAGE_WIDE(ARG(image));
    REBINT h = VAL_IMAGE_HIGH(ARG(image));

    memset(&bmfh, 0, sizeof(bmfh));
    bmfh.bfType[0] = 'B';
    bmfh.bfType[1] = 'M';
    bmfh.bfSize = 14 + 40 + h * WADJUST(w);
    bmfh.bfOffBits = 14 + 40;

    // Create binary string:
    REBSER *bin = Make_Binary(bmfh.bfSize);
    cp = BIN_HEAD(bin);
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

    dp = cast(REBCNT *, VAL_IMAGE_BITS(ARG(image)));
    dp += w * h - w;

    for (y = 0; y<h; y++) {
        for (i = 0; i<w; i++) {
            v = (REBYTE*)dp++;
            cp[0] = v[C_B];
            cp[1] = v[C_G];
            cp[2] = v[C_R];
            cp += 3;
        }
        i = w * 3;
        while (i++ % 4)
            *cp++ = 0;
        dp -= 2 * w;
    }

    TERM_BIN_LEN(bin, bmfh.bfSize);
    Init_Binary(D_OUT, bin);
    return R_OUT;
}


#include "tmp-mod-bmp-last.h"
