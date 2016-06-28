//
//  File: %t-image.c
//  Summary: "image datatype"
//  Section: datatypes
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2016 Rebol Open Source Contributors
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

#include "sys-core.h"

#define CLEAR_IMAGE(p, x, y) memset(p, 0, x * y * sizeof(u32))

#define RESET_IMAGE(p, l) do { \
    REBCNT *start = (REBCNT*)p; \
    REBCNT *stop = start + l; \
    while (start < stop) *start++ = 0xff000000; \
} while(0)


//
//  CT_Image: C
//
REBINT CT_Image(const RELVAL *a, const RELVAL *b, REBINT mode)
{
    if (mode < 0)
        return -1;

    if (
        VAL_IMAGE_WIDE(a) == VAL_IMAGE_WIDE(a)
        && VAL_IMAGE_HIGH(b) == VAL_IMAGE_HIGH(b)
    ) {
        return (0 == Cmp_Value(a, b, LOGICAL(mode == 1))) ? 1 : 0;
    }

    return 0;
}


void Copy_Image_Value(REBVAL *out, const REBVAL *arg, REBINT len)
{
    len = MAX(len, 0); // no negatives
    len = MIN(len, cast(REBINT, VAL_IMAGE_LEN(arg)));

    REBINT w = VAL_IMAGE_WIDE(arg);
    w = MAX(w, 1);

    REBINT h;
    if (len <= w) {
        h = 1;
        w = len;
    }
    else
        h = len / w;

    if (w == 0)
        h = 0;

    REBSER *series = Make_Image(w, h, TRUE);
    Val_Init_Image(out, series);
    memcpy(VAL_IMAGE_HEAD(out), VAL_IMAGE_DATA(arg), w * h * 4);
}


//
//  MAKE_Image: C
//
void MAKE_Image(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    if (IS_IMAGE(arg)) {
        //
        // make image! img
        //
        Copy_Image_Value(out, arg, VAL_IMAGE_LEN(arg));
    }
    else if (IS_BLANK(arg) || (IS_BLOCK(arg) && VAL_ARRAY_LEN_AT(arg) == 0)) {
        //
        // make image! [] (or none)
        //
        Val_Init_Image(out, Make_Image(0, 0, TRUE));
    }
    else if (IS_PAIR(arg)) {
        //
        // make image! size
        //
        REBINT w = VAL_PAIR_X_INT(arg);
        REBINT h = VAL_PAIR_Y_INT(arg);
        w = MAX(w, 0);
        h = MAX(h, 0);
        Val_Init_Image(out, Make_Image(w, h, TRUE));
    }
    else if (IS_BLOCK(arg)) {
        //
        // make image! [size rgb alpha index]
        //
        RELVAL *item = VAL_ARRAY_AT(arg);

        if (!IS_PAIR(item)) goto bad_make;

        REBINT w = VAL_PAIR_X_INT(item);
        REBINT h = VAL_PAIR_Y_INT(item);
        if (w < 0 || h < 0) goto bad_make;

        REBSER *img = Make_Image(w, h, FALSE);
        if (!img) goto bad_make;

        Val_Init_Image(out, img);

        REBYTE *ip = IMG_DATA(img); // image pointer
        REBCNT size = w * h;

        ++item;

        if (IS_END(item)) {
            //
            // make image! [10x20]... already done
        }
        else if (IS_BINARY(item)) {

            // Load image data:
            Bin_To_RGB(ip, size, VAL_BIN_AT(item), VAL_LEN_AT(item) / 3);
            ++item;

            // !!! Review handling of END here; was not explicit before and
            // just fell through the binary and integer tests...

            // Load alpha channel data:
            if (NOT_END(item) && IS_BINARY(item)) {
                Bin_To_Alpha(ip, size, VAL_BIN_AT(item), VAL_LEN_AT(item));
    //          VAL_IMAGE_TRANSP(value)=VITT_ALPHA;
                ++item;
            }

            if (NOT_END(item) && IS_INTEGER(item)) {
                VAL_INDEX(out) = (Int32s(KNOWN(item), 1) - 1);
                ++item;
            }
        }
        else if (IS_TUPLE(item)) {
            Fill_Rect(cast(REBCNT*, ip), TO_PIXEL_TUPLE(item), w, w, h, TRUE);
            ++item;
            if (IS_INTEGER(item)) {
                Fill_Alpha_Rect(
                    cast(REBCNT*, ip), cast(REBYTE, VAL_INT32(item)), w, w, h
                );
    //          VAL_IMAGE_TRANSP(value)=VITT_ALPHA;
                ++item;
            }
        }
        else if (IS_BLOCK(item)) {
            REBCNT bad_index;
            if (Array_Has_Non_Tuple(&bad_index, item))
                fail (Error_Invalid_Arg_Core(
                    VAL_ARRAY_AT_HEAD(item, bad_index),
                    IS_SPECIFIC(item)
                        ? VAL_SPECIFIER(KNOWN(item))
                        : VAL_SPECIFIER(arg)
                ));

            Tuples_To_RGBA(
                ip, size, KNOWN(VAL_ARRAY_AT(item)), VAL_LEN_AT(item)
            );
        }
        else
            goto bad_make;

        assert(IS_IMAGE(out));
    }
    else
        fail (Error_Invalid_Type(VAL_TYPE(arg)));

    return;

bad_make:
    fail (Error_Bad_Make(kind, arg));
}


//
//  TO_Image: C
//
void TO_Image(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    if (IS_IMAGE(arg)) {
        Copy_Image_Value(out, arg, VAL_IMAGE_LEN(arg));
    }
    else if (IS_GOB(arg)) {
        REBSER *series = OS_GOB_TO_IMAGE(VAL_GOB(arg));
        if (!series)
            fail (Error_Bad_Make(REB_IMAGE, arg));
        Val_Init_Image(out, series);
    }
    else if (IS_BINARY(arg)) {
        REBINT diff = VAL_LEN_AT(arg) / 4;
        if (diff == 0)
            fail (Error_Bad_Make(REB_IMAGE, arg));

        REBINT w;
        if (diff < 100) w = diff;
        else if (diff < 10000) w = 100;
        else w = 500;

        REBINT h = diff / w;
        if (w * h < diff) h++; // partial line

        REBSER *series = Make_Image(w, h, TRUE);
        Val_Init_Image(out, series);
        Bin_To_RGBA(
            IMG_DATA(series),
            w * h,
            VAL_BIN_AT(arg),
            VAL_LEN_AT(arg) / 4,
            FALSE
        );
    }
    else
        fail (Error_Invalid_Type(VAL_TYPE(arg)));
}


//
//  Reset_Height: C
// 
// Set height based on tail and width.
//
void Reset_Height(REBVAL *value)
{
    REBCNT w = VAL_IMAGE_WIDE(value);
    VAL_IMAGE_HIGH(value) = w ? (VAL_LEN_HEAD(value) / w) : 0;
}


//
//  Set_Pixel_Tuple: C
//
void Set_Pixel_Tuple(REBYTE *dp, const REBVAL *tuple)
{
    // Tuple to pixel.
    const REBYTE *tup = VAL_TUPLE(tuple);

    dp[C_R] = tup[0];
    dp[C_G] = tup[1];
    dp[C_B] = tup[2];
    if (VAL_TUPLE_LEN(tuple) > 3)
        dp[C_A] = tup[3];
    else
        dp[C_A] = 0xff;
}


//
//  Set_Tuple_Pixel: C
//
void Set_Tuple_Pixel(REBYTE *dp, REBVAL *tuple)
{
    // Pixel to tuple.
    REBYTE *tup = VAL_TUPLE(tuple);

    VAL_RESET_HEADER(tuple, REB_TUPLE);
    VAL_TUPLE_LEN(tuple) = 4;
    tup[0] = dp[C_R];
    tup[1] = dp[C_G];
    tup[2] = dp[C_B];
    tup[3] = dp[C_A];
}

//
//  Fill_Line: C
//
void Fill_Line(REBCNT *ip, REBCNT color, REBCNT len, REBOOL only)
{
    if (only) {// only RGB, do not touch Alpha
        color &= 0xffffff;
        for (; len > 0; len--, ip++) *ip = (*ip & 0xff000000) | color;
    } else
        for (; len > 0; len--) *ip++ = color;
}


//
//  Fill_Rect: C
//
void Fill_Rect(REBCNT *ip, REBCNT color, REBCNT w, REBINT dupx, REBINT dupy, REBOOL only)
{
    for (; dupy > 0; dupy--, ip += w)
        Fill_Line(ip, color, dupx, only);
}


//
//  Fill_Alpha_Line: C
//
void Fill_Alpha_Line(REBYTE *rgba, REBYTE alpha, REBINT len)
{
    for (; len > 0; len--, rgba += 4)
        rgba[C_A] = alpha;
}


//
//  Fill_Alpha_Rect: C
//
void Fill_Alpha_Rect(REBCNT *ip, REBYTE alpha, REBINT w, REBINT dupx, REBINT dupy)
{
    for (; dupy > 0; dupy--, ip += w)
        Fill_Alpha_Line((REBYTE *)ip, alpha, dupx);
}


//
//  Find_Color: C
//
REBCNT *Find_Color(REBCNT *ip, REBCNT color, REBCNT len, REBOOL only)
{
    if (only) { // only RGB, do not touch Alpha
        for (; len > 0; len--, ip++)
            if (color == (*ip & 0x00ffffff)) return ip;
    } else {
        for (; len > 0; len--, ip++)
            if (color == *ip) return ip;
    }
    return 0;
}


//
//  Find_Alpha: C
//
REBCNT *Find_Alpha(REBCNT *ip, REBCNT alpha, REBCNT len)
{
    for (; len > 0; len--, ip++) {
        if (alpha == (*ip >> 24)) return ip;
    }
    return 0;
}


//
//  RGB_To_Bin: C
//
void RGB_To_Bin(REBYTE *bin, REBYTE *rgba, REBINT len, REBOOL alpha)
{
    // Convert internal image (integer) to RGB/A order binary string:
    if (alpha) {
        for (; len > 0; len--, rgba += 4, bin += 4) {
            bin[0] = rgba[C_R];
            bin[1] = rgba[C_G];
            bin[2] = rgba[C_B];
            bin[3] = rgba[C_A];
        }
    } else {
        // Only the RGB part:
        for (; len > 0; len--, rgba += 4, bin += 3) {
            bin[0] = rgba[C_R];
            bin[1] = rgba[C_G];
            bin[2] = rgba[C_B];
        }
    }
}


//
//  Bin_To_RGB: C
//
void Bin_To_RGB(REBYTE *rgba, REBCNT size, REBYTE *bin, REBCNT len)
{
    if (len > size) len = size; // avoid over-run

    // Convert RGB binary string to internal image (integer), no alpha:
    for (; len > 0; len--, rgba += 4, bin += 3) {
        rgba[C_R] = bin[0];
        rgba[C_G] = bin[1];
        rgba[C_B] = bin[2];
    }
}


//
//  Bin_To_RGBA: C
//
void Bin_To_RGBA(REBYTE *rgba, REBCNT size, REBYTE *bin, REBINT len, REBOOL only)
{
    if (len > (REBINT)size) len = size; // avoid over-run

    // Convert from RGBA format to internal image (integer):
    for (; len > 0; len--, rgba += 4, bin += 4) {
        rgba[C_R] = bin[0];
        rgba[C_G] = bin[1];
        rgba[C_B] = bin[2];
        if (!only) rgba[C_A] = bin[3];
    }
}


//
//  Alpha_To_Bin: C
//
void Alpha_To_Bin(REBYTE *bin, REBYTE *rgba, REBINT len)
{
    for (; len > 0; len--, rgba += 4)
        *bin++ = rgba[C_A];
}


//
//  Bin_To_Alpha: C
//
void Bin_To_Alpha(REBYTE *rgba, REBCNT size, REBYTE *bin, REBINT len)
{
    if (len > (REBINT)size) len = size; // avoid over-run

    for (; len > 0; len--, rgba += 4)
        rgba[C_A] = *bin++;
}


//
//  Array_Has_Non_Tuple: C
//
// Checks the given ANY-ARRAY! REBVAL from its current index position to
// the end to see if any of its contents are not TUPLE!.  If so, returns
// TRUE and `index_out` will contain the index position from the head of
// the array of the non-tuple.  Otherwise returns FALSE.
//
REBOOL Array_Has_Non_Tuple(REBCNT *index_out, RELVAL *blk)
{
    REBCNT len;

    assert(ANY_ARRAY(blk));

    len = VAL_LEN_HEAD(blk);
    *index_out = VAL_INDEX(blk);

    for (; *index_out < len; (*index_out)++)
        if (!IS_TUPLE(VAL_ARRAY_AT_HEAD(blk, *index_out)))
            return TRUE;

    return FALSE;
}


//
//  Tuples_To_RGBA: C
//
void Tuples_To_RGBA(REBYTE *rgba, REBCNT size, REBVAL *blk, REBCNT len)
{
    REBYTE *bin;

    if (len > size) len = size; // avoid over-run

    for (; len > 0; len--, rgba += 4, blk++) {
        bin = VAL_TUPLE(blk);
        rgba[C_R] = bin[0];
        rgba[C_G] = bin[1];
        rgba[C_B] = bin[2];
        rgba[C_A] = bin[3];
    }
}


//
//  Image_To_RGBA: C
//
void Image_To_RGBA(REBYTE *rgba, REBYTE *bin, REBINT len)
{
    // Convert from internal image (integer) to RGBA binary order:
    for (; len > 0; len--, rgba += 4, bin += 4) {
        bin[0] = rgba[C_R];
        bin[1] = rgba[C_G];
        bin[2] = rgba[C_B];
        bin[3] = rgba[C_A];
    }
}

#ifdef NEED_ARGB_TO_BGR
REBCNT ARGB_To_BGR(REBCNT i)
{
    return
        ((i & 0x00ff0000) >> 16) | // red
        ((i & 0x0000ff00)) |       // green
        ((i & 0x000000ff) << 16);  // blue
}
#endif

//
//  Mold_Image_Data: C
//
void Mold_Image_Data(const REBVAL *value, REB_MOLD *mold)
{
    REBUNI *up;
    REBCNT len;
    REBCNT size;
    REBCNT *data;
    REBYTE* pixel;

    Emit(mold, "IxI #{", VAL_IMAGE_WIDE(value), VAL_IMAGE_HIGH(value));

    // Output RGB image:
    size = VAL_IMAGE_LEN(value); // # pixels (from index to tail)
    data = (REBCNT *)VAL_IMAGE_DATA(value);
    up = Prep_Uni_Series(mold, (size * 6) + (size / 10) + 1);

    for (len = 0; len < size; len++) {
        pixel = (REBYTE*)data++;
        if ((len % 10) == 0) *up++ = LF;
        up = Form_RGB_Uni(up, TO_RGBA_COLOR(pixel[C_R],pixel[C_G],pixel[C_B],pixel[C_A]));
    }

    // Output Alpha channel, if it has one:
    if (Image_Has_Alpha(value, FALSE)) {

        Append_Unencoded(mold->series, "\n} #{");

        up = Prep_Uni_Series(mold, (size * 2) + (size / 10) + 1);

        data = (REBCNT *)VAL_IMAGE_DATA(value);
        for (len = 0; len < size; len++) {
            if ((len % 10) == 0) *up++ = LF;
            up = Form_Hex2_Uni(up, *data++ >> 24);
        }
    }
    *up = 0; // tail already set from Prep.

    Append_Unencoded(mold->series, "\n}");
}


//
//  Make_Image_Binary: C
//
REBSER *Make_Image_Binary(const REBVAL *image)
{
    REBSER *ser;
    REBINT len;
    len =  VAL_IMAGE_LEN(image) * 4;
    ser = Make_Binary(len);
    SET_SERIES_LEN(ser, len);
    Image_To_RGBA(VAL_IMAGE_DATA(image), QUAD_HEAD(ser), VAL_IMAGE_LEN(image));
    return ser;
}


//
//  Make_Image: C
// 
// Allocate and initialize an image.
// If error is TRUE, throw error on bad size.
// Return zero on oversized image.
//
REBSER *Make_Image(REBCNT w, REBCNT h, REBOOL error)
{
    REBSER *img;

    if (w > 0xFFFF || h > 0xFFFF) {
        if (error)
            fail (Error(RE_SIZE_LIMIT, Get_Type(REB_IMAGE)));
        return NULL;
    }

    img = Make_Series(w * h + 1, sizeof(u32), MKS_NONE);
    SET_SERIES_LEN(img, w * h);
    RESET_IMAGE(SER_DATA_RAW(img), SER_LEN(img)); //length in 'pixels'
    IMG_WIDE(img) = w;
    IMG_HIGH(img) = h;
    return img;
}


//
//  Clear_Image: C
// 
// Clear image data.
//
void Clear_Image(REBVAL *img)
{
    REBCNT w = VAL_IMAGE_WIDE(img);
    REBCNT h = VAL_IMAGE_HIGH(img);
    REBYTE *p = VAL_IMAGE_HEAD(img);
    CLEAR_IMAGE(p, w, h);
}


//
//  Modify_Image: C
// 
// Insert or change image
// ACTION value arg /part len /only /dup count
//
REBVAL *Modify_Image(struct Reb_Frame *frame_, REBCNT action)
{
    REBVAL  *value = D_ARG(1);
    REBVAL  *arg   = D_ARG(2);
    REBVAL  *len   = D_ARG(4);
    REBVAL  *count = D_ARG(7);
    REBINT  part = 1; // /part len
    REBINT  partx, party;
    REBINT  dup = 1;  // /dup count
    REBINT  dupx, dupy;
    REBOOL  only = FALSE; // /only
    REBCNT  index = VAL_INDEX(value);
    REBCNT  tail = VAL_LEN_HEAD(value);
    REBCNT  n;
    REBINT  x;
    REBINT  w;
    REBINT  y;
    REBYTE  *ip;

    if (!(w = VAL_IMAGE_WIDE(value))) return value;

    if (action == SYM_APPEND) {
        index = tail;
        action = SYM_INSERT;
    }

    x = index % w; // offset on the line
    y = index / w; // offset line

    if (D_REF(5)) only = TRUE;

    // Validate that block arg is all tuple values:
    if (IS_BLOCK(arg) && Array_Has_Non_Tuple(&n, arg))
        fail (Error_Invalid_Arg_Core(
            VAL_ARRAY_AT_HEAD(arg, n), VAL_SPECIFIER(arg)
        ));

    // Get the /dup refinement. It specifies fill size.
    if (D_REF(6)) {
        if (IS_INTEGER(count)) {
            dup = VAL_INT32(count);
            dup = MAX(dup, 0);
            if (dup == 0) return value;
        } else if (IS_PAIR(count)) { // rectangular dup
            dupx = VAL_PAIR_X_INT(count);
            dupy = VAL_PAIR_Y_INT(count);
            dupx = MAX(dupx, 0);
            dupx = MIN(dupx, (REBINT)w - x); // clip dup width
            dupy = MAX(dupy, 0);
            if (action != SYM_INSERT)
                dupy = MIN(dupy, (REBINT)VAL_IMAGE_HIGH(value) - y);
            else
                dup = dupy * w;
            if (dupx == 0 || dupy == 0) return value;
        }
        else
            fail (Error_Invalid_Type(VAL_TYPE(count)));
    }

    // Get the /part refinement. Only allowed when arg is a series.
    if (D_REF(3)) {
        if (IS_BINARY(arg)) {
            if (IS_INTEGER(len)) {
                part = VAL_INT32(len);
            } else if (IS_BINARY(len)) {
                part = (VAL_INDEX(len) - VAL_INDEX(arg)) / 4;
            } else
                fail (Error_Invalid_Arg(len));
            part = MAX(part, 0);
        } else if (IS_IMAGE(arg)) {
            if (IS_INTEGER(len)) {
                part = VAL_INT32(len);
                part = MAX(part, 0);
            } else if (IS_IMAGE(len)) {
                if (!VAL_IMAGE_WIDE(len)) fail (Error_Invalid_Arg(len));
                partx = VAL_INDEX(len) - VAL_INDEX(arg);
                party = partx / VAL_IMAGE_WIDE(len);
                party = MAX(party, 1);
                partx = MIN(partx, (REBINT)VAL_IMAGE_WIDE(arg));
                goto len_compute;
            } else if (IS_PAIR(len)) {
                partx = VAL_PAIR_X_INT(len);
                party = VAL_PAIR_Y_INT(len);
            len_compute:
                partx = MAX(partx, 0);
                partx = MIN(partx, (REBINT)w - x); // clip part width
                party = MAX(party, 0);
                if (action != SYM_INSERT)
                    party = MIN(party, (REBINT)VAL_IMAGE_HIGH(value) - y);
                else
                    part = party * w;
                if (partx == 0 || party == 0) return value;
            }
            else
                fail (Error_Invalid_Type(VAL_TYPE(len)));
        }
        else
            fail (Error_Invalid_Arg(arg)); // /part not allowed
    }
    else {
        if (IS_IMAGE(arg)) { // Use image for /part sizes
            partx = VAL_IMAGE_WIDE(arg);
            party = VAL_IMAGE_HIGH(arg);
            partx = MIN(partx, (REBINT)w - x); // clip part width
            if (action != SYM_INSERT)
                party = MIN(party, (REBINT)VAL_IMAGE_HIGH(value) - y);
            else
                part = party * w;
        } else if (IS_BINARY(arg)) {
            part = VAL_LEN_AT(arg) / 4;
        } else if (IS_BLOCK(arg)) {
            part = VAL_LEN_AT(arg);
        }
        else if (!IS_INTEGER(arg) && !IS_TUPLE(arg))
            fail (Error_Invalid_Type(VAL_TYPE(arg)));
    }

    // Expand image data if necessary:
    if (action == SYM_INSERT) {
        if (index > tail) index = tail;
        Expand_Series(VAL_SERIES(value), index, dup * part);
        RESET_IMAGE(VAL_BIN(value) + (index * 4), dup * part); //length in 'pixels'
        Reset_Height(value);
        tail = VAL_LEN_HEAD(value);
        only = FALSE;
    }
    ip = VAL_IMAGE_HEAD(value);

    // Handle the datatype of the argument.
    if (IS_INTEGER(arg) || IS_TUPLE(arg)) { // scalars
        if (index + dup > tail) dup = tail - index; // clip it
        ip += index * 4;
        if (IS_INTEGER(arg)) { // Alpha channel
            REBINT arg_int = VAL_INT32(arg);
            if ((arg_int < 0) || (arg_int > 255))
                fail (Error_Out_Of_Range(arg));
            if (IS_PAIR(count)) // rectangular fill
                Fill_Alpha_Rect(
                    cast(REBCNT*, ip), cast(REBYTE, arg_int), w, dupx, dupy
                );
            else
                Fill_Alpha_Line(ip, cast(REBYTE, arg_int), dup);
        }
        else if (IS_TUPLE(arg)) { // RGB
            if (IS_PAIR(count)) // rectangular fill
                Fill_Rect((REBCNT *)ip, TO_PIXEL_TUPLE(arg), w, dupx, dupy, only);
            else
                Fill_Line((REBCNT *)ip, TO_PIXEL_TUPLE(arg), dup, only);
        }
    } else if (IS_IMAGE(arg)) {
        Copy_Rect_Data(value, x, y, partx, party, arg, 0, 0); // dst dx dy w h src sx sy
    } else if (IS_BINARY(arg)) {
        if (index + part > tail) part = tail - index; // clip it
        ip += index * 4;
        for (; dup > 0; dup--, ip += part * 4)
            Bin_To_RGBA(ip, part, VAL_BIN_AT(arg), part, only);
    } else if (IS_BLOCK(arg)) {
        if (index + part > tail) part = tail - index; // clip it
        ip += index * 4;
        for (; dup > 0; dup--, ip += part * 4)
            Tuples_To_RGBA(ip, part, KNOWN(VAL_ARRAY_AT(arg)), part);
    }
    else
        fail (Error_Invalid_Type(VAL_TYPE(arg)));

    Reset_Height(value);

    if (action == SYM_APPEND) VAL_INDEX(value) = 0;
    return value;
}


//
//  Find_Image: C
// 
// Finds a value in a series and returns the series at the start of it.
// 
//      1 image
//      2 value [<opt> any-value!]
//      3 /part {Limits the search to a given length or position.}
//      4 range [any-number! any-series! port!]
//      5 /only {ignore alpha value.}
//      6 /case - ignored
//      7 /any  - ignored
//      8 /with - ignored
//      9 wild  - ignored
//     10 /skip - ignored
//     11 size  - ignored
//     12 /match {Performs comparison and returns the tail of the match.}
//     13 /tail  {Returns the end of the string.}
//     14 /last  {Backwards from end of string.}
//     15 /reverse {Backwards from the current position.}
//
REBVAL *Find_Image(struct Reb_Frame *frame_)
{
    REBVAL  *value = D_ARG(1);
    REBVAL  *arg   = D_ARG(2);
    REBCNT  index = VAL_INDEX(value);
    REBCNT  tail = VAL_LEN_HEAD(value);
    REBCNT  len;
    REBCNT  *ip = (REBCNT *)VAL_IMAGE_DATA(value); // NOTE ints not bytes
    REBCNT  *p;
    REBINT  n;
    REBOOL  only = FALSE;
    REBYTE  no_refs[10] = {5, 6, 7, 8, 9, 10, 13, 14}; // ref - 1 (invalid refinements)

    len = tail - index;
    if (!len) goto find_blank;

    for (n = 0; n < 8; n++) // (zero based)
        if (D_REF(no_refs[n]))
            fail (Error(RE_BAD_REFINE));

    if (IS_TUPLE(arg)) {
        only = LOGICAL(VAL_TUPLE_LEN(arg) < 4);
        if (D_REF(5)) only = TRUE; // /only flag
        p = Find_Color(ip, TO_PIXEL_TUPLE(arg), len, only);
    } else if (IS_INTEGER(arg)) {
        n = VAL_INT32(arg);
        if (n < 0 || n > 255) fail (Error_Out_Of_Range(arg));
        p = Find_Alpha(ip, n, len);
    } else if (IS_IMAGE(arg)) {
        p = 0;
    } else if (IS_BINARY(arg)) {
        p = 0;
    }
    else
        fail (Error_Invalid_Type(VAL_TYPE(arg)));

    // Post process the search (failure or apply /match and /tail):
    if (p) {
        n = (REBCNT)(p - (REBCNT *)VAL_IMAGE_HEAD(value));
        if (D_REF(11)) { // match
            if (n != (REBINT)index) goto find_blank;
            n++;
        } else if (D_REF(12)) n++; // /tail
        index = n;
        VAL_INDEX(value) = index;
        return value;
    }
find_blank:
    return BLANK_VALUE;
}


//
//  Image_Has_Alpha: C
//
REBOOL Image_Has_Alpha(const REBVAL *v, REBOOL save)
{
    int i;
    REBCNT *p;

//  if (VAL_IMAGE_TRANSP_TYPE(v)==VITT_NONE) return FALSE;
//  if (VAL_IMAGE_TRANSP_TYPE(v)==VITT_ALPHA) return TRUE;

    p = (REBCNT *)VAL_IMAGE_HEAD(v);
    i = VAL_IMAGE_WIDE(v)*VAL_IMAGE_HIGH(v);
    for(; i > 0; i--) {
        if (~*p++ & 0xff000000) {
//          if (save) VAL_IMAGE_TRANSP(v) = VITT_ALPHA;
            return TRUE;
        }
    }
//  if (save) VAL_IMAGE_TRANSP(v) = VITT_NONE;

    return FALSE;
}


//
//  Copy_Rect_Data: C
//
void Copy_Rect_Data(REBVAL *dst, REBINT dx, REBINT dy, REBINT w, REBINT h, REBVAL *src, REBINT sx, REBINT sy)
{
    REBCNT  *sbits, *dbits;

    if (w <= 0 || h <= 0) return;

    // Clip at edges:
    if ((REBCNT)(dx + w) > VAL_IMAGE_WIDE(dst)) w = VAL_IMAGE_WIDE(dst) - dx;
    if ((REBCNT)(dy + h) > VAL_IMAGE_HIGH(dst)) h = VAL_IMAGE_HIGH(dst) - dy;

    sbits = VAL_IMAGE_BITS(src) + sy * VAL_IMAGE_WIDE(src) + sx;
    dbits = VAL_IMAGE_BITS(dst) + dy * VAL_IMAGE_WIDE(dst) + dx;
    while (h--) {
        memcpy(dbits, sbits, w*4);
        sbits += VAL_IMAGE_WIDE(src);
        dbits += VAL_IMAGE_WIDE(dst);
    }
}


//
//  Complement_Image: C
//
static REBSER *Complement_Image(REBVAL *value)
{
    REBCNT *img = (REBCNT*) VAL_IMAGE_DATA(value);
    REBCNT *out;
    REBINT len = VAL_IMAGE_LEN(value);
    REBSER *ser;

    ser = Make_Image(VAL_IMAGE_WIDE(value), VAL_IMAGE_HIGH(value), TRUE);
    out = (REBCNT*) IMG_DATA(ser);

    for (; len > 0; len --) *out++ = ~ *img++;

    return ser;
}


//
//  REBTYPE: C
//
REBTYPE(Image)
{
    REBVAL  *value = D_ARG(1);
    REBVAL  *arg = D_ARGC > 1 ? D_ARG(2) : NULL;
    REBSER  *series;
    REBINT  index;
    REBINT  tail;
    REBINT  diff, len, w, h;
    REBVAL  *val;

    // Clip index if past tail:
    series = VAL_SERIES(value);
    index = VAL_INDEX(value);
    tail = (REBINT)SER_LEN(series);
    if (index > tail) index = tail;

    // Check must be in this order (to avoid checking a non-series value);
    if (action >= SYM_TAKE && action <= SYM_SORT)
        FAIL_IF_LOCKED_SERIES(series);

    // Dispatch action:
    switch (action) {

    case SYM_HEAD:
        VAL_INDEX(value) = 0;
        break;

    case SYM_TAIL:
        VAL_INDEX(value) = (REBCNT)tail;
        break;

    case SYM_HEAD_Q:
        return (index == 0) ? R_TRUE : R_FALSE;

    case SYM_TAIL_Q:
        return (index >= tail) ? R_TRUE : R_FALSE;

    case SYM_NEXT:
        if (index < tail) VAL_INDEX(value)++;
        break;

    case SYM_BACK:
        if (index > 0) VAL_INDEX(value)--;
        break;

    case SYM_COMPLEMENT:
        series = Complement_Image(value);
        Val_Init_Image(value, series); // use series var not func
        break;

    case SYM_INDEX_OF:
        if (D_REF(2)) {
            VAL_RESET_HEADER(D_OUT, REB_PAIR);
            VAL_PAIR_X(D_OUT) = cast(REBD32, index % VAL_IMAGE_WIDE(value));
            VAL_PAIR_Y(D_OUT) = cast(REBD32, index / VAL_IMAGE_WIDE(value));
            return R_OUT;
        }
        else {
            SET_INTEGER(D_OUT, index + 1);
            return R_OUT;
        }
        // fallthrough
    case SYM_LENGTH:
        SET_INTEGER(D_OUT, tail > index ? tail - index : 0);
        return R_OUT;

    case SYM_PICK:
        Pick_Path(D_OUT, value, arg, 0);
        return R_OUT;

    case SYM_POKE:
        Pick_Path(D_OUT, value, arg, D_ARG(3));
        *D_OUT = *D_ARG(3);
        return R_OUT;

    case SYM_SKIP:
    case SYM_AT:
        // This logic is somewhat complicated by the fact that INTEGER args use
        // base-1 indexing, but PAIR args use base-0.
        if (IS_PAIR(arg)) {
            if (action == SYM_AT) action = SYM_SKIP;
            diff = (VAL_PAIR_Y_INT(arg) * VAL_IMAGE_WIDE(value) + VAL_PAIR_X_INT(arg)) +
                ((action == SYM_SKIP) ? 0 : 1);
        } else
            diff = Get_Num_From_Arg(arg);

        index += diff;
        if (action == SYM_SKIP) {
            if (IS_LOGIC(arg)) index--;
        } else {
            if (diff > 0) index--; // For at, pick, poke.
        }

        if (index > tail)
            index = tail;
        else if (index < 0)
            index = 0;
        VAL_INDEX(value) = (REBCNT)index;
        break;

#ifdef obsolete
        if (action == A_SKIP || action == A_AT) {
        }

        if (diff == 0 || index < 0 || index >= tail) {
            if (action == A_POKE)
                fail (Error_Out_Of_Range(arg));
            goto is_blank;
        }

        if (action == A_POKE) {
            REBINT *dp = QUAD_SKIP(series, index));
            REBINT n;

            arg = D_ARG(3);
            if (IS_TUPLE(arg) && (IS_IMAGE(value))) {
                Set_Pixel_Tuple(QUAD_SKIP(series, index), arg);
                //*dp = (long) (VAL_TUPLE_LEN(arg) < 4) ?
                //  ((*dp & 0xff000000) | (VAL_TUPLE(arg)[0] << 16) | (VAL_TUPLE(arg)[1] << 8) | (VAL_TUPLE(arg)[2])) :
                //  ((VAL_TUPLE(arg)[3] << 24) | (VAL_TUPLE(arg)[0] << 16) | (VAL_TUPLE(arg)[1] << 8) | (VAL_TUPLE(arg)[2]));
                *D_OUT = *arg;
                return R_OUT;
            }
            if (IS_INTEGER(arg) && VAL_INT64(arg) > 0 && VAL_INT64(arg) < 255)
                n = VAL_INT32(arg);
            else if (IS_CHAR(arg))
                n = VAL_CHAR(arg);
            else
                fail (Error_Invalid_Arg(arg));

            *dp = (*dp & 0xffffff) | (n << 24);
            *D_OUT = *arg;
            return R_OUT; //was value;

        } else {
            Set_Tuple_Pixel(QUAD_SKIP(series, index), D_OUT);
            return R_OUT;
        }
        break;
#endif

    case SYM_CLEAR:   // clear series
        if (index < tail) {
            SET_SERIES_LEN(VAL_SERIES(value), cast(REBCNT, index));
            Reset_Height(value);
        }
        break;

    case SYM_REMOVE:  // remove series /part count
        if (D_REF(2)) {
            val = D_ARG(3);
            if (IS_INTEGER(val)) {
                len = VAL_INT32(val);
            } else if (IS_IMAGE(val)) {
                if (!VAL_IMAGE_WIDE(val)) fail (Error_Invalid_Arg(val));
                len = VAL_INDEX(val) - VAL_INDEX(value); // may not be same, is ok
            }
            else
                fail (Error_Invalid_Type(VAL_TYPE(val)));
        }
        else len = 1;

        index = (REBINT)VAL_INDEX(value);
        if (index < tail && len != 0) {
            Remove_Series(series, VAL_INDEX(value), len);
        }
        Reset_Height(value);
        break;

    case SYM_APPEND:
    case SYM_INSERT:  // insert ser val /part len /only /dup count
    case SYM_CHANGE:  // change ser val /part len /only /dup count
        value = Modify_Image(frame_, action); // sets DS_OUT
        break;

    case SYM_FIND:    // find   ser val /part len /only /case /any /with wild /match /tail
        Find_Image(frame_); // sets DS_OUT
        break;

    case SYM_COPY:  // copy series /part len
        if (!D_REF(2)) {
            arg = value;
            goto makeCopy;
        }
        arg = D_ARG(3); // can be image, integer, pair.
        if (IS_IMAGE(arg)) {
            if (VAL_SERIES(arg) != VAL_SERIES(value))
                fail (Error_Invalid_Arg(arg));
            len = VAL_INDEX(arg) - VAL_INDEX(value);
            arg = value;
            goto makeCopy2;
        }
        if (IS_INTEGER(arg)) {
            len = VAL_INT32(arg);
            arg = value;
            goto makeCopy2;
        }
        if (IS_PAIR(arg)) {
            w = VAL_PAIR_X_INT(arg);
            h = VAL_PAIR_Y_INT(arg);
            w = MAX(w, 0);
            h = MAX(h, 0);
            diff = MIN(VAL_LEN_HEAD(value), VAL_INDEX(value)); // index offset
            diff = MAX(0, diff);
            index = VAL_IMAGE_WIDE(value); // width
            if (index) {
                len = diff / index; // compute y offset
                diff %= index; // compute x offset
            } else len = diff = 0; // avoid div zero
            w = MIN(w, index - diff); // img-width - x-pos
            h = MIN(h, (int)(VAL_IMAGE_HIGH(value) - len)); // img-high - y-pos
            series = Make_Image(w, h, TRUE);
            Val_Init_Image(D_OUT, series);
            Copy_Rect_Data(D_OUT, 0, 0, w, h, value, diff, len);
//          VAL_IMAGE_TRANSP(D_OUT) = VAL_IMAGE_TRANSP(value);
            return R_OUT;
        }
        fail (Error_Invalid_Type(VAL_TYPE(arg)));

makeCopy:
        // Src image is arg.
        len = VAL_IMAGE_LEN(arg);
makeCopy2:
        Copy_Image_Value(D_OUT, arg, len);
        return R_OUT;

    default:
        fail (Error_Illegal_Action(VAL_TYPE(value), action));
    }

    *D_OUT = *value;
    return R_OUT;
}


//
//  PD_Image: C
//
REBINT PD_Image(REBPVS *pvs)
{
    RELVAL *data = pvs->value;
    const REBVAL *sel = pvs->selector;
    const REBVAL *setval;
    REBINT n;
    REBINT len;
    REBYTE *src;
    REBSER *nser;
    REBCNT *dp;

    REBSER *series = VAL_SERIES(data);
    REBINT index = cast(REBINT, VAL_INDEX(data));

    len = VAL_LEN_HEAD(data) - index;
    len = MAX(len, 0);
    src = VAL_IMAGE_DATA(data);

    if (IS_PAIR(sel)) n = (VAL_PAIR_Y_INT(sel) * VAL_IMAGE_WIDE(data) + VAL_PAIR_X_INT(sel)) + 1;
    else if (IS_INTEGER(sel)) n = VAL_INT32(sel);
    else if (IS_DECIMAL(sel)) n = (REBINT)VAL_DECIMAL(sel);
    else if (IS_LOGIC(sel))   n = (VAL_LOGIC(sel) ? 1 : 2);
    else if (IS_WORD(sel)) {
        if (!pvs->opt_setval) {
            switch (VAL_WORD_SYM(sel)) {
            case SYM_SIZE:
                VAL_RESET_HEADER(pvs->store, REB_PAIR);
                VAL_PAIR_X(pvs->store) = (REBD32)VAL_IMAGE_WIDE(data);
                VAL_PAIR_Y(pvs->store) = (REBD32)VAL_IMAGE_HIGH(data);
                break;

            case SYM_RGB:
                nser = Make_Binary(len * 3);
                SET_SERIES_LEN(nser, len * 3);
                RGB_To_Bin(QUAD_HEAD(nser), src, len, FALSE);
                Val_Init_Binary(pvs->store, nser);
                break;

            case SYM_ALPHA:
                nser = Make_Binary(len);
                SET_SERIES_LEN(nser, len);
                Alpha_To_Bin(QUAD_HEAD(nser), src, len);
                Val_Init_Binary(pvs->store, nser);
                break;

            default:
                fail (Error_Bad_Path_Select(pvs));
            }
            return PE_USE_STORE;
        }
        else {
            FAIL_IF_LOCKED_SERIES(series);
            setval = pvs->opt_setval;

            switch (VAL_WORD_SYM(sel)) {
            case SYM_SIZE:
                if (!IS_PAIR(setval) || !VAL_PAIR_X(setval))
                    fail (Error_Bad_Path_Set(pvs));

                VAL_IMAGE_WIDE(data) = VAL_PAIR_X_INT(setval);
                VAL_IMAGE_HIGH(data) = MIN(
                    VAL_PAIR_Y_INT(setval),
                    cast(REBINT, VAL_LEN_HEAD(data) / VAL_PAIR_X_INT(setval))
                );
                break;

            case SYM_RGB:
                if (IS_TUPLE(setval)) {
                    Fill_Line(
                        cast(REBCNT*, src), TO_PIXEL_TUPLE(setval), len, TRUE
                    );
                } else if (IS_INTEGER(setval)) {
                    n = VAL_INT32(setval);
                    if (n < 0 || n > 255)
                        fail (Error_Bad_Path_Range(pvs));

                    Fill_Line(
                        cast(REBCNT*, src),
                        TO_PIXEL_COLOR(n,n,n,0xFF),
                        len,
                        TRUE
                    );
                }
                else if (IS_BINARY(setval)) {
                    Bin_To_RGB(
                        src,
                        len,
                        VAL_BIN_AT(setval),
                        VAL_LEN_AT(setval) / 3
                    );
                }
                else
                    fail (Error_Bad_Path_Set(pvs));
                break;

            case SYM_ALPHA:
                if (IS_INTEGER(setval)) {
                    n = VAL_INT32(setval);
                    if (n < 0 || n > 255)
                        fail (Error_Bad_Path_Range(pvs));

                    Fill_Alpha_Line(src, (REBYTE)n, len);
                }
                else if (IS_BINARY(setval)) {
                    Bin_To_Alpha(
                        src,
                        len,
                        VAL_BIN_AT(setval),
                        VAL_LEN_AT(setval)
                    );
                }
                else
                    fail (Error_Bad_Path_Set(pvs));
                break;

            default:
                fail (Error_Bad_Path_Select(pvs));
            }
            return PE_OK;
        }
    }
    else
        fail (Error_Bad_Path_Select(pvs));

    // Handle index path:
    index += n;
    if (n > 0) index--;

    // Out of range:
    if (n == 0 || index < 0 || index >= cast(REBINT, SER_LEN(series))) {
        if (pvs->opt_setval)
            fail (Error_Bad_Path_Set(pvs));

        return PE_NONE;
    }

    // Get the pixel:
    if (!pvs->opt_setval) {
        Set_Tuple_Pixel(QUAD_SKIP(series, index), pvs->store);
        return PE_USE_STORE;
    }

    FAIL_IF_LOCKED_SERIES(series);
    setval = pvs->opt_setval;

    // Set the pixel:
    if (IS_TUPLE(setval)) {
        assert(IS_IMAGE(data)); // there was an && clause in this if before
        Set_Pixel_Tuple(QUAD_SKIP(series, index), setval);
        return PE_OK;
    }

    // Set the alpha only:
    if (
        IS_INTEGER(setval)
        && VAL_INT64(setval) > 0
        && VAL_INT64(setval) < 255
    ) {
        n = VAL_INT32(setval);
    }
    else if (IS_CHAR(setval))
        n = VAL_CHAR(setval);
    else
        fail (Error_Bad_Path_Range(pvs));

    dp = cast(REBCNT*, QUAD_SKIP(series, index));
    *dp = (*dp & 0xffffff) | (n << 24);
    return PE_OK;
}
