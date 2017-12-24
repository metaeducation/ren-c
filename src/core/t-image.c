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
    Init_Image(out, series);
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
        Init_Image(out, Make_Image(0, 0, TRUE));
    }
    else if (IS_PAIR(arg)) {
        //
        // make image! size
        //
        REBINT w = VAL_PAIR_X_INT(arg);
        REBINT h = VAL_PAIR_Y_INT(arg);
        w = MAX(w, 0);
        h = MAX(h, 0);
        Init_Image(out, Make_Image(w, h, TRUE));
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

        Init_Image(out, img);

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
            if (Array_Has_Non_Tuple(&bad_index, item)) {
                REBSPC *derived = Derive_Specifier(VAL_SPECIFIER(arg), item);
                fail (Error_Invalid_Arg_Core(
                    VAL_ARRAY_AT_HEAD(item, bad_index),
                    derived
                ));
            }

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
    assert(kind == REB_IMAGE);
    UNUSED(kind);

    if (IS_IMAGE(arg)) {
        Copy_Image_Value(out, arg, VAL_IMAGE_LEN(arg));
    }
    else if (IS_GOB(arg)) {
        REBVAL *image = OS_GOB_TO_IMAGE(VAL_GOB(arg));
        if (image == NULL)
            fail (Error_Bad_Make(REB_IMAGE, arg)); // not GUI build...
        Move_Value(out, image); // what are the GC semantics here?
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
        Init_Image(out, series);
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
// Output RGB image data, and then alpha channel (if it has one)
//
void Mold_Image_Data(const REBVAL *value, REB_MOLD *mold)
{
    REBCNT size = VAL_IMAGE_LEN(value); // # pixels (from index to tail)
    REBCNT *data = cast(REBCNT*, VAL_IMAGE_DATA(value));

    Emit(mold, "IxI #{", VAL_IMAGE_WIDE(value), VAL_IMAGE_HIGH(value));

    // !!! Actually accurate?
    //
    REBYTE *bp = Prep_Mold_Overestimated(mold, (size * 6) + (size / 10) + 1);

    REBCNT len;
    for (len = 0; len < size; len++) {
        REBYTE* pixel = cast(REBYTE*, data++);
        if ((len % 10) == 0)
            *bp++ = LF;
        bp = Form_RGB_Utf8(
            bp, TO_RGBA_COLOR(pixel[C_R], pixel[C_G], pixel[C_B], pixel[C_A])
        );
    }

    // Output Alpha channel, if it has one:
    if (Image_Has_Alpha(value)) {

        Append_Unencoded(mold->series, "\n} #{");

        // !!! Actually accurate?
        //
        bp = Prep_Mold_Overestimated(mold, (size * 2) + (size / 10) + 1);

        data = cast(REBCNT*, VAL_IMAGE_DATA(value));
        for (len = 0; len < size; len++) {
            if ((len % 10) == 0)
                *bp++ = LF;
            bp = Form_Hex2_UTF8(bp, *data++ >> 24);
        }
    }

    *bp = '\0'; // tail already set from Prep (so it thinks it guessed right)

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
    if (w > 0xFFFF || h > 0xFFFF) {
        if (error)
            fail (Error_Size_Limit_Raw(Get_Type(REB_IMAGE)));
        return NULL;
    }

    REBSER *img = Make_Series(w * h + 1, sizeof(u32));
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
//
REBVAL *Modify_Image(REBFRM *frame_, REBCNT action)
{
    INCLUDE_PARAMS_OF_INSERT; // currently must have same frame as CHANGE

    REBVAL  *value = ARG(series); // !!! confusing, very old (unused?) code!
    REBVAL  *arg   = ARG(value);
    REBVAL  *len   = ARG(limit); // void if no /PART
    REBVAL  *count = ARG(count); // void if no /DUP

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

    if (REF(only))
        only = TRUE;

    // Validate that block arg is all tuple values:
    if (IS_BLOCK(arg) && Array_Has_Non_Tuple(&n, arg))
        fail (Error_Invalid_Arg_Core(
            VAL_ARRAY_AT_HEAD(arg, n), VAL_SPECIFIER(arg)
        ));

    if (REF(dup)) { // "it specifies fill size"
        if (IS_INTEGER(count)) {
            dup = VAL_INT32(count);
            dup = MAX(dup, 0);
            if (dup == 0) return value;
        }
        else if (IS_PAIR(count)) { // rectangular dup
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

    if (REF(part)) { // only allowed when arg is a series
        if (IS_BINARY(arg)) {
            if (IS_INTEGER(len)) {
                part = VAL_INT32(len);
            } else if (IS_BINARY(len)) {
                part = (VAL_INDEX(len) - VAL_INDEX(arg)) / 4;
            } else
                fail (len);
            part = MAX(part, 0);
        }
        else if (IS_IMAGE(arg)) {
            if (IS_INTEGER(len)) {
                part = VAL_INT32(len);
                part = MAX(part, 0);
            }
            else if (IS_IMAGE(len)) {
                if (VAL_IMAGE_WIDE(len) == 0)
                    fail (len);

                partx = VAL_INDEX(len) - VAL_INDEX(arg);
                party = partx / VAL_IMAGE_WIDE(len);
                party = MAX(party, 1);
                partx = MIN(partx, (REBINT)VAL_IMAGE_WIDE(arg));
                goto len_compute;
            }
            else if (IS_PAIR(len)) {
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
            fail (arg); // /part not allowed
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
        }
        else if (IS_BINARY(arg)) {
            part = VAL_LEN_AT(arg) / 4;
        }
        else if (IS_BLOCK(arg)) {
            part = VAL_LEN_AT(arg);
        }
        else if (!IS_INTEGER(arg) && !IS_TUPLE(arg))
            fail (Error_Invalid_Type(VAL_TYPE(arg)));
    }

    // Expand image data if necessary:
    if (action == SYM_INSERT) {
        if (index > tail) index = tail;
        Expand_Series(VAL_SERIES(value), index, dup * part);

        //length in 'pixels'
        RESET_IMAGE(VAL_BIN_HEAD(value) + (index * 4), dup * part);
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
    }
    else if (IS_BINARY(arg)) {
        if (index + part > tail) part = tail - index; // clip it
        ip += index * 4;
        for (; dup > 0; dup--, ip += part * 4)
            Bin_To_RGBA(ip, part, VAL_BIN_AT(arg), part, only);
    }
    else if (IS_BLOCK(arg)) {
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
// Finds a value in a series and returns the series at the start of it.  For
// parameters of FIND, see the action definition.
//
// !!! old and very broken code, untested and probably (hopefully) not
// used by R3-View... (?)
//
void Find_Image(REBFRM *frame_)
{
    INCLUDE_PARAMS_OF_FIND;

    REBVAL  *value = ARG(series);
    REBVAL  *arg = ARG(value);
    REBCNT  index = VAL_INDEX(value);
    REBCNT  tail = VAL_LEN_HEAD(value);
    REBCNT  *ip = (REBCNT *)VAL_IMAGE_DATA(value); // NOTE ints not bytes
    REBCNT  *p;
    REBINT  n;

    REBCNT len = tail - index;
    if (len == 0) {
        Init_Void(D_OUT);
        return;
    }

    // !!! There is a general problem with refinements and actions in R3-Alpha
    // in terms of reporting when a refinement was ignored.  This is a
    // problem that archetype-based dispatch will need to address.
    //
    if (
        REF(case)
        || REF(skip)
        || REF(last)
        || REF(match)
        || REF(part)
        || REF(reverse)
    ){
        UNUSED(PAR(limit));
        UNUSED(PAR(size));
        fail (Error_Bad_Refines_Raw());
    }

    REBOOL only; // initialization would be crossed by goto
    only = FALSE;
    if (IS_TUPLE(arg)) {
        only = LOGICAL(VAL_TUPLE_LEN(arg) < 4);
        if (REF(only)) only = TRUE;
        p = Find_Color(ip, TO_PIXEL_TUPLE(arg), len, only);
    }
    else if (IS_INTEGER(arg)) {
        n = VAL_INT32(arg);
        if (n < 0 || n > 255) fail (Error_Out_Of_Range(arg));
        p = Find_Alpha(ip, n, len);
    }
    else if (IS_IMAGE(arg)) {
        p = 0;
    }
    else if (IS_BINARY(arg)) {
        p = 0;
    }
    else
        fail (Error_Invalid_Type(VAL_TYPE(arg)));

    if (p == 0) {
        Init_Void(D_OUT);
        return;
    }

    // Post process the search (failure or apply /match and /tail):

    Move_Value(D_OUT, value);
    n = (REBCNT)(p - (REBCNT *)VAL_IMAGE_HEAD(value));
    if (REF(match)) {
        if (n != cast(REBINT, index)) {
            Init_Void(D_OUT);
            return;
        }
        n++;
    }
    else
        if (REF(tail))
            ++n;

    VAL_INDEX(value) = n;
    return;
}


//
//  Image_Has_Alpha: C
//
// !!! See code in R3-Alpha for VITT_ALPHA and the `save` flag.
//
REBOOL Image_Has_Alpha(const REBVAL *v)
{
    REBCNT *p = cast(REBCNT*, VAL_IMAGE_HEAD(v));

    int i = VAL_IMAGE_WIDE(v) * VAL_IMAGE_HIGH(v);
    for(; i > 0; i--) {
        if (~*p++ & 0xff000000)
            return TRUE;
    }

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
    if (dx + w > VAL_IMAGE_WIDE(dst))
        w = VAL_IMAGE_WIDE(dst) - dx;
    if (dy + h > VAL_IMAGE_HIGH(dst))
        h = VAL_IMAGE_HIGH(dst) - dy;

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
//  ML_Image: C
//
void MF_Image(REB_MOLD *mo, const RELVAL *v, REBOOL form)
{
    UNUSED(form); // no difference between MOLD and FORM at this time

    Pre_Mold(mo, v);
    if (GET_MOLD_FLAG(mo, MOLD_FLAG_ALL)) {
        DECLARE_LOCAL (head);
        Move_Value(head, const_KNOWN(v));
        VAL_INDEX(head) = 0; // mold all of it
        Mold_Image_Data(head, mo);
        Post_Mold(mo, v);
    }
    else {
        Append_Utf8_Codepoint(mo->series, '[');
        Mold_Image_Data(const_KNOWN(v), mo);
        Append_Utf8_Codepoint(mo->series, ']');
        End_Mold(mo);
    }
}


//
//  REBTYPE: C
//
REBTYPE(Image)
{
    REBVAL  *value = D_ARG(1);
    REBVAL  *arg = D_ARGC > 1 ? D_ARG(2) : NULL;
    REBINT  diff, len, w, h;
    REBVAL  *val;

    REBSER *series = VAL_SERIES(value);
    REBINT index = VAL_INDEX(value);
    REBINT tail = cast(REBINT, SER_LEN(series));

    // Clip index if past tail:
    //
    if (index > tail)
        index = tail;

    // Check must be in this order (to avoid checking a non-series value);
    if (action >= SYM_TAKE_P && action <= SYM_SORT)
        FAIL_IF_READ_ONLY_SERIES(series);

    // Dispatch action:
    switch (action) {

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value)); // accounted for by value above
        REBSYM property = VAL_WORD_SYM(ARG(property));
        assert(property != SYM_0);

        switch (property) {
        case SYM_HEAD:
            VAL_INDEX(value) = 0;
            break;

        case SYM_TAIL:
            VAL_INDEX(value) = cast(REBCNT, tail);
            break;

        case SYM_HEAD_Q:
            return R_FROM_BOOL(LOGICAL(index == 0));

        case SYM_TAIL_Q:
            return R_FROM_BOOL(LOGICAL(index >= tail));

        case SYM_XY:
            SET_PAIR(
                D_OUT,
                index % VAL_IMAGE_WIDE(value),
                index / VAL_IMAGE_WIDE(value)
            );
            return R_OUT;

        case SYM_INDEX:
            Init_Integer(D_OUT, index + 1);
            return R_OUT;

        case SYM_LENGTH:
            Init_Integer(D_OUT, tail > index ? tail - index : 0);
            return R_OUT;

        default:
            break;
        }

        break; }

    case SYM_COMPLEMENT:
        series = Complement_Image(value);
        Init_Image(value, series); // use series var not func
        Move_Value(D_OUT, value);
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

        VAL_INDEX(value) = cast(REBCNT, index);
        Move_Value(D_OUT, value);
        return R_OUT;

    case SYM_CLEAR:   // clear series
        if (index < tail) {
            SET_SERIES_LEN(VAL_SERIES(value), cast(REBCNT, index));
            Reset_Height(value);
        }
        Move_Value(D_OUT, value);
        return R_OUT;

    case SYM_REMOVE: {
        INCLUDE_PARAMS_OF_REMOVE;

        UNUSED(PAR(series));

        if (REF(map)) {
            UNUSED(ARG(key));
            fail (Error_Bad_Refines_Raw());
        }

        if (REF(part)) {
            val = ARG(limit);
            if (IS_INTEGER(val)) {
                len = VAL_INT32(val);
            }
            else if (IS_IMAGE(val)) {
                if (!VAL_IMAGE_WIDE(val))
                    fail (val);
                len = VAL_INDEX(val) - VAL_INDEX(value); // not same is ok
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
        Move_Value(D_OUT, value);
        return R_OUT; }

    case SYM_APPEND:
    case SYM_INSERT:  // insert ser val /part len /only /dup count
    case SYM_CHANGE:  // change ser val /part len /only /dup count
        value = Modify_Image(frame_, action); // sets DS_OUT
        Move_Value(D_OUT, value);
        return R_OUT;

    case SYM_FIND:
        Find_Image(frame_); // sets DS_OUT
        return R_OUT;

    case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;

        UNUSED(PAR(value));

        if (REF(deep))
            fail (Error_Bad_Refines_Raw());

        if (REF(types)) {
            UNUSED(ARG(kinds));
            fail (Error_Bad_Refines_Raw());
        }

        if (NOT(REF(part))) {
            arg = value;
            goto makeCopy;
        }
        arg = ARG(limit); // can be image, integer, pair.
        if (IS_IMAGE(arg)) {
            if (VAL_SERIES(arg) != VAL_SERIES(value))
                fail (arg);
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
            Init_Image(D_OUT, series);
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
        return R_OUT; }

    default:
        break;
    }

    fail (Error_Illegal_Action(VAL_TYPE(value), action));
}


inline static REBOOL Adjust_Image_Pick_Index_Is_Valid(
    REBINT *index, // gets adjusted
    const REBVAL *value, // image
    const REBVAL *picker
) {
    REBINT n;
    if (IS_PAIR(picker)) {
        n = (
            VAL_PAIR_Y_INT(picker) * VAL_IMAGE_WIDE(value)
            + VAL_PAIR_X_INT(picker)
        ) + 1;
    }
    else if (IS_INTEGER(picker))
        n = VAL_INT32(picker);
    else if (IS_DECIMAL(picker))
        n = cast(REBINT, VAL_DECIMAL(picker));
    else if (IS_LOGIC(picker))
        n = VAL_LOGIC(picker) ? 1 : 2;
    else
        fail (picker);

    *index += n;
    if (n > 0)
        (*index)--;

    if (n == 0 || *index < 0 || *index >= cast(REBINT, VAL_LEN_HEAD(value)))
        return FALSE; // out of range

    return TRUE;
}


//
//  Pick_Image: C
//
void Pick_Image(REBVAL *out, const REBVAL *value, const REBVAL *picker)
{
    REBSER *series = VAL_SERIES(value);

    REBINT index = cast(REBINT, VAL_INDEX(value));
    REBINT len = VAL_LEN_HEAD(value) - index;
    len = MAX(len, 0);

    REBYTE *src = VAL_IMAGE_DATA(value);

    if (IS_WORD(picker)) {
        switch (VAL_WORD_SYM(picker)) {
        case SYM_SIZE:
            SET_PAIR(
                out,
                VAL_IMAGE_WIDE(value),
                VAL_IMAGE_HIGH(value)
            );
            break;

        case SYM_RGB: {
            REBSER *nser = Make_Binary(len * 3);
            SET_SERIES_LEN(nser, len * 3);
            RGB_To_Bin(QUAD_HEAD(nser), src, len, FALSE);
            Init_Binary(out, nser);
            break; }

        case SYM_ALPHA: {
            REBSER *nser = Make_Binary(len);
            SET_SERIES_LEN(nser, len);
            Alpha_To_Bin(QUAD_HEAD(nser), src, len);
            Init_Binary(out, nser);
            break; }

        default:
            fail (picker);
        }
        return;
    }

    if (Adjust_Image_Pick_Index_Is_Valid(&index, value, picker))
        Set_Tuple_Pixel(QUAD_SKIP(series, index), out);
    else
        Init_Void(out);
}


//
//  Poke_Image_Fail_If_Read_Only: C
//
void Poke_Image_Fail_If_Read_Only(
    REBVAL *value,
    const REBVAL *picker,
    const REBVAL *poke
) {
    REBSER *series = VAL_SERIES(value);
    FAIL_IF_READ_ONLY_SERIES(series);

    REBINT index = cast(REBINT, VAL_INDEX(value));
    REBINT len = VAL_LEN_HEAD(value) - index;
    len = MAX(len, 0);

    REBYTE *src = VAL_IMAGE_DATA(value);

    if (IS_WORD(picker)) {
        switch (VAL_WORD_SYM(picker)) {
        case SYM_SIZE:
            if (!IS_PAIR(poke) || !VAL_PAIR_X(poke))
                fail (poke);

            VAL_IMAGE_WIDE(value) = VAL_PAIR_X_INT(poke);
            VAL_IMAGE_HIGH(value) = MIN(
                VAL_PAIR_Y_INT(poke),
                cast(REBINT, VAL_LEN_HEAD(value) / VAL_PAIR_X_INT(poke))
            );
            break;

        case SYM_RGB:
            if (IS_TUPLE(poke)) {
                Fill_Line(
                    cast(REBCNT*, src), TO_PIXEL_TUPLE(poke), len, TRUE
                );
            } else if (IS_INTEGER(poke)) {
                REBINT byte = VAL_INT32(poke);
                if (byte < 0 || byte > 255)
                    fail (Error_Out_Of_Range(poke));

                Fill_Line(
                    cast(REBCNT*, src),
                    TO_PIXEL_COLOR(byte, byte, byte, 0xFF),
                    len,
                    TRUE
                );
            }
            else if (IS_BINARY(poke)) {
                Bin_To_RGB(
                    src,
                    len,
                    VAL_BIN_AT(poke),
                    VAL_LEN_AT(poke) / 3
                );
            }
            else
                fail (poke);
            break;

        case SYM_ALPHA:
            if (IS_INTEGER(poke)) {
                REBINT n = VAL_INT32(poke);
                if (n < 0 || n > 255)
                    fail (Error_Out_Of_Range(poke));

                Fill_Alpha_Line(src, cast(REBYTE, n), len);
            }
            else if (IS_BINARY(poke)) {
                Bin_To_Alpha(
                    src,
                    len,
                    VAL_BIN_AT(poke),
                    VAL_LEN_AT(poke)
                );
            }
            else
                fail (poke);
            break;

        default:
            fail (picker);
        }
        return;
    }

    if (!Adjust_Image_Pick_Index_Is_Valid(&index, value, picker))
        fail (Error_Out_Of_Range(picker));

    if (IS_TUPLE(poke)) { // set whole pixel
        Set_Pixel_Tuple(QUAD_SKIP(series, index), poke);
        return;
    }

    // set the alpha only

    REBINT alpha;
    if (
        IS_INTEGER(poke)
        && VAL_INT64(poke) > 0
        && VAL_INT64(poke) < 255
    ) {
        alpha = VAL_INT32(poke);
    }
    else if (IS_CHAR(poke))
        alpha = VAL_CHAR(poke);
    else
        fail (Error_Out_Of_Range(poke));

    REBCNT *dp = cast(REBCNT*, QUAD_SKIP(series, index));
    *dp = (*dp & 0xffffff) | (alpha << 24);
}


//
//  PD_Image: C
//
REB_R PD_Image(REBPVS *pvs, const REBVAL *picker, const REBVAL *opt_setval)
{
    if (opt_setval != NULL) {
        Poke_Image_Fail_If_Read_Only(pvs->out, picker, opt_setval);
        return R_INVISIBLE;
    }

    Pick_Image(pvs->out, pvs->out, picker);
    return R_OUT;
}
