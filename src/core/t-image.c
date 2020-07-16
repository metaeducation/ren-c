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

inline static void RESET_IMAGE(REBYTE *p, REBCNT num_pixels) {
    REBYTE *start = p;
    REBYTE *stop = start + (num_pixels * 4);
    while (start < stop) {
        *start++ = 0; // red
        *start++ = 0; // green
        *start++ = 0; // blue
        *start++ = 0xff; // opaque alpha, R=G=B as 0 means black pixel
    }
}

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
        return (0 == Cmp_Value(a, b, mode == 1)) ? 1 : 0;
    }

    return 0;
}


void Copy_Image_Value(REBVAL *out, const REBVAL *arg, REBINT len)
{
    len = MAX(len, 0); // no negatives
    len = MIN(len, cast(REBINT, VAL_IMAGE_LEN_AT(arg)));

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

    Make_Image(out, w, h);
    memcpy(VAL_IMAGE_HEAD(out), VAL_IMAGE_AT(arg), w * h * 4);
}


//
//  MAKE_Image: C
//
REB_R MAKE_Image(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    if (IS_IMAGE(arg)) {
        //
        // make image! img
        //
        Copy_Image_Value(out, arg, VAL_IMAGE_LEN_AT(arg));
    }
    else if (IS_BLANK(arg) || (IS_BLOCK(arg) && VAL_ARRAY_LEN_AT(arg) == 0)) {
        //
        // make image! [] (or none)
        //
        Make_Image(out, 0, 0);
    }
    else if (IS_PAIR(arg)) {
        //
        // make image! size
        //
        REBINT w = VAL_PAIR_X_INT(arg);
        REBINT h = VAL_PAIR_Y_INT(arg);
        w = MAX(w, 0);
        h = MAX(h, 0);
        Make_Image(out, w, h);
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

        Make_Image(out, w, h);

        REBYTE *ip = VAL_IMAGE_HEAD(out); // image pointer
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
            REBYTE pixel[4];
            Set_Pixel_Tuple(pixel, item);
            Fill_Rect(ip, pixel, w, w, h, true);
            ++item;
            if (IS_INTEGER(item)) {
                Fill_Alpha_Rect(
                    ip, cast(REBYTE, VAL_INT32(item)), w, w, h
                );
    //          VAL_IMAGE_TRANSP(value)=VITT_ALPHA;
                ++item;
            }
        }
        else if (IS_BLOCK(item)) {
            REBCNT bad_index;
            if (Array_Has_Non_Tuple(&bad_index, item)) {
                REBSPC *derived = Derive_Specifier(VAL_SPECIFIER(arg), item);
                fail (Error_Invalid_Core(
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

    return out;

bad_make:
    fail (Error_Bad_Make(kind, arg));
}


//
//  TO_Image: C
//
REB_R TO_Image(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    assert(kind == REB_IMAGE);
    UNUSED(kind);

    if (IS_IMAGE(arg)) {
        Copy_Image_Value(out, arg, VAL_IMAGE_LEN_AT(arg));
    }
    else if (IS_GOB(arg)) {
        REBVAL *image = OS_GOB_TO_IMAGE(arg);
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

        Make_Image(out, w, h);
        Bin_To_RGBA(
            VAL_IMAGE_HEAD(out),
            w * h,
            VAL_BIN_AT(arg),
            VAL_LEN_AT(arg) / 4,
            false
        );
    }
    else
        fail (Error_Invalid_Type(VAL_TYPE(arg)));

    return out;
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
void Set_Pixel_Tuple(REBYTE *dp, const RELVAL *tuple)
{
    const REBYTE *tup = VAL_TUPLE(tuple);
    dp[0] = tup[0]; // red
    dp[1] = tup[1]; // green
    dp[2] = tup[2]; // blue
    if (VAL_TUPLE_LEN(tuple) > 3)
        dp[3] = tup[3]; // alpha
    else
        dp[3] = 0xff; // default alpha to opaque
}


//
//  Init_Tuple_From_Pixel: C
//
REBVAL *Init_Tuple_From_Pixel(RELVAL *out, const REBYTE *dp)
{
    RESET_CELL(out, REB_TUPLE);
    REBYTE *tup = VAL_TUPLE(out);
    VAL_TUPLE_LEN(out) = 4;
    tup[0] = dp[0]; // red
    tup[1] = dp[1]; // green
    tup[2] = dp[2]; // blue
    tup[3] = dp[3]; // alpha
    return cast(REBVAL*, out);
}


//
//  Fill_Line: C
//
void Fill_Line(REBYTE *ip, const REBYTE pixel[4], REBCNT len, bool only)
{
    for (; len > 0; len--) {
        *ip++ = pixel[0]; // red
        *ip++ = pixel[1]; // green
        *ip++ = pixel[2]; // blue
        if (only)
            ++ip; // only RGB, don't change alpha...just skip it
        else
            *ip++ = pixel[3]; // alpha
    }
}


//
//  Fill_Rect: C
//
void Fill_Rect(
    REBYTE *ip,
    const REBYTE pixel[4],
    REBCNT w,
    REBINT dupx,
    REBINT dupy,
    bool only
){
    for (; dupy > 0; dupy--, ip += (w * 4))
        Fill_Line(ip, pixel, dupx, only);
}


//
//  Fill_Alpha_Line: C
//
void Fill_Alpha_Line(REBYTE *rgba, REBYTE alpha, REBINT len)
{
    for (; len > 0; len--, rgba += 4)
        rgba[3] = alpha;
}


//
//  Fill_Alpha_Rect: C
//
void Fill_Alpha_Rect(REBYTE *ip, REBYTE alpha, REBINT w, REBINT dupx, REBINT dupy)
{
    for (; dupy > 0; dupy--, ip += (w * 4))
        Fill_Alpha_Line(ip, alpha, dupx);
}


//
//  Find_Color: C
//
REBYTE *Find_Color(
    REBYTE *ip,
    const REBYTE pixel[4],
    REBCNT len,
    bool only
){
    for (; len > 0; len--, ip += 4) {
        if (ip[0] != pixel[0])
            continue; // red not equal
        if (ip[1] != pixel[1])
            continue; // green not equal
        if (ip[2] != pixel[2])
            continue; // blue not equal
        if (not only and ip[3] != pixel[3])
            continue; // paying attention to alpha, and not equal
        return ip;
    }
    return nullptr;
}


//
//  Find_Alpha: C
//
REBYTE *Find_Alpha(REBYTE *ip, REBYTE alpha, REBCNT len)
{
    for (; len > 0; len--, ip += 4) {
        if (alpha == ip[3])
            return ip; // alpha equal in rgba[3]
    }
    return nullptr;
}


//
//  RGB_To_Bin: C
//
void RGB_To_Bin(REBYTE *bin, REBYTE *rgba, REBINT len, bool alpha)
{
    if (alpha) {
        for (; len > 0; len--, rgba += 4, bin += 4) {
            bin[0] = rgba[0];
            bin[1] = rgba[1];
            bin[2] = rgba[2];
            bin[3] = rgba[3];
        }
    } else {
        // Only the RGB part:
        for (; len > 0; len--, rgba += 4, bin += 3) {
            bin[0] = rgba[0];
            bin[1] = rgba[1];
            bin[2] = rgba[2];
        }
    }
}


//
//  Bin_To_RGB: C
//
void Bin_To_RGB(REBYTE *rgba, REBCNT size, const REBYTE *bin, REBCNT len)
{
    if (len > size)
        len = size; // avoid over-run

    for (; len > 0; len--, rgba += 4, bin += 3) {
        rgba[0] = bin[0]; // red
        rgba[1] = bin[1]; // green
        rgba[2] = bin[2]; // blue
        // don't touch alpha of destination
    }
}


//
//  Bin_To_RGBA: C
//
void Bin_To_RGBA(
    REBYTE *rgba,
    REBCNT size,
    const REBYTE *bin,
    REBINT len,
    bool only
){
    if (len > (REBINT)size) len = size; // avoid over-run

    for (; len > 0; len--, rgba += 4, bin += 4) {
        rgba[0] = bin[0]; // red
        rgba[1] = bin[1]; // green
        rgba[2] = bin[2]; // blue
        if (not only)
            rgba[3] = bin[3]; // write alpha of destination if requested
    }
}


//
//  Alpha_To_Bin: C
//
void Alpha_To_Bin(REBYTE *bin, REBYTE *rgba, REBINT len)
{
    for (; len > 0; len--, rgba += 4)
        *bin++ = rgba[3];
}


//
//  Bin_To_Alpha: C
//
void Bin_To_Alpha(REBYTE *rgba, REBCNT size, REBYTE *bin, REBINT len)
{
    if (len > (REBINT)size) len = size; // avoid over-run

    for (; len > 0; len--, rgba += 4)
        rgba[3] = *bin++;
}


//
//  Array_Has_Non_Tuple: C
//
// Checks the given ANY-ARRAY! REBVAL from its current index position to
// the end to see if any of its contents are not TUPLE!.  If so, returns
// true and `index_out` will contain the index position from the head of
// the array of the non-tuple.  Otherwise returns false.
//
bool Array_Has_Non_Tuple(REBCNT *index_out, RELVAL *blk)
{
    REBCNT len;

    assert(ANY_ARRAY(blk));

    len = VAL_LEN_HEAD(blk);
    *index_out = VAL_INDEX(blk);

    for (; *index_out < len; (*index_out)++)
        if (!IS_TUPLE(VAL_ARRAY_AT_HEAD(blk, *index_out)))
            return true;

    return false;
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
        rgba[0] = bin[0];
        rgba[1] = bin[1];
        rgba[2] = bin[2];
        rgba[3] = bin[3];
    }
}


//
//  Mold_Image_Data: C
//
// Output RGB image data, and then alpha channel (if it has one)
//
void Mold_Image_Data(const REBVAL *value, REB_MOLD *mold)
{
    REBCNT num_pixels = VAL_IMAGE_LEN_AT(value); // # from index to tail
    const REBYTE *rgba = VAL_IMAGE_AT(value);

    Emit(mold, "IxI #{", VAL_IMAGE_WIDE(value), VAL_IMAGE_HIGH(value));

    // !!! Actually accurate?
    //
    REBYTE *bp = Prep_Mold_Overestimated(
        mold,
        (num_pixels * 6) + (num_pixels / 10) + 1
    );

    REBCNT i;
    for (i = 0; i < num_pixels; ++i, rgba += 4) {
        if ((i % 10) == 0)
            *bp++ = LF;
        bp = Form_RGB_Utf8(bp, rgba);
    }

    // Output Alpha channel, if it has one:
    if (Image_Has_Alpha(value)) {

        Append_Unencoded(mold->series, "\n} #{");

        // !!! Actually accurate?
        //
        bp = Prep_Mold_Overestimated(
            mold,
            (num_pixels * 2) + (num_pixels / 10) + 1
        );

        rgba = VAL_IMAGE_AT(value);
        for (i = 0; i < num_pixels; ++i, rgba += 4) {
            if ((i % 10) == 0)
                *bp++ = LF;
            bp = Form_Hex2_UTF8(bp, rgba[3]); // alpha
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
    size_t size = VAL_IMAGE_LEN_AT(image) * 4;
    REBSER *bin = Make_Binary(size);
    SET_SERIES_LEN(bin, size);
    memcpy(BIN_HEAD(bin), VAL_IMAGE_AT(image), size);
    return bin;
}


//
//  Make_Image: C
//
// Creates WxH image, black pixels, all opaque.
//
void Make_Image(REBVAL *out, REBCNT w, REBCNT h)
{
    // !!! Temporary size storage limit for images, applicable only because
    // the size is poked into a hidden location of the REBSER node.  This
    // will not be a problem when stored in a separate PAIR!
    //
    if (w > 0xFFFF || h > 0xFFFF)
        fail (Error_Size_Limit_Raw(Datatype_From_Kind(REB_IMAGE)));

    // Series is created with 4-byte elements.  One reason for this is so that
    // the common series mechanics (for handling things like NEXT or BACK)
    // will move in pixel-size units.  So long as IMAGE! is implemented with
    // the common series routines, this will be necessary--it should change
    // when it becomes a user defined type with a PAIR! and a BINARY!.
    //
    REBSER *img = Make_Ser(w * h + 1, 4);
    SET_SERIES_LEN(img, (w * h));

    RESET_IMAGE(SER_DATA_RAW(img), (w * h)); // length in 'pixels'

    IMG_WIDE(img) = w;
    IMG_HIGH(img) = h;

    Init_Any_Series(out, REB_IMAGE, img);
}


//
//  Clear_Image: C
//
// Clear image data (sets R=G=B=A to 0)
//
void Clear_Image(REBVAL *img)
{
    REBCNT w = VAL_IMAGE_WIDE(img);
    REBCNT h = VAL_IMAGE_HIGH(img);
    REBYTE *p = VAL_IMAGE_HEAD(img);
    memset(p, 0, w * h * 4);
}


//
//  Modify_Image: C
//
// Insert or change image
//
REB_R Modify_Image(REBFRM *frame_, REBVAL *verb)
{
    INCLUDE_PARAMS_OF_INSERT; // currently must have same frame as CHANGE

    if (REF(line))
        fail (Error_Bad_Refines_Raw());

    REBVAL  *value = ARG(series); // !!! confusing, very old (unused?) code!
    REBVAL  *arg   = ARG(value);
    REBVAL  *len   = ARG(limit); // void if no /PART
    REBVAL  *count = ARG(count); // void if no /DUP

    REBINT  part = 1; // /part len
    REBINT  partx, party;
    REBINT  dup = 1;  // /dup count
    REBINT  dupx, dupy;
    bool only = false; // /only
    REBCNT  index = VAL_INDEX(value);
    REBCNT  tail = VAL_LEN_HEAD(value);
    REBCNT  n;
    REBINT  x;
    REBINT  w;
    REBINT  y;
    REBYTE  *ip;

    if (!(w = VAL_IMAGE_WIDE(value))) return value;

    REBSYM sym = VAL_WORD_SYM(verb);
    if (sym == SYM_APPEND) {
        index = tail;
        sym = SYM_INSERT;
    }

    x = index % w; // offset on the line
    y = index / w; // offset line

    if (REF(only))
        only = true;

    // Validate that block arg is all tuple values:
    if (IS_BLOCK(arg) && Array_Has_Non_Tuple(&n, arg))
        fail (Error_Invalid_Core(
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
            if (sym != SYM_INSERT)
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
                fail (Error_Invalid(len));
            part = MAX(part, 0);
        }
        else if (IS_IMAGE(arg)) {
            if (IS_INTEGER(len)) {
                part = VAL_INT32(len);
                part = MAX(part, 0);
            }
            else if (IS_IMAGE(len)) {
                if (VAL_IMAGE_WIDE(len) == 0)
                    fail (Error_Invalid(len));

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
                if (sym != SYM_INSERT)
                    party = MIN(party, (REBINT)VAL_IMAGE_HIGH(value) - y);
                else
                    part = party * w;
                if (partx == 0 || party == 0) return value;
            }
            else
                fail (Error_Invalid_Type(VAL_TYPE(len)));
        }
        else
            fail (Error_Invalid(arg)); // /part not allowed
    }
    else {
        if (IS_IMAGE(arg)) { // Use image for /part sizes
            partx = VAL_IMAGE_WIDE(arg);
            party = VAL_IMAGE_HIGH(arg);
            partx = MIN(partx, (REBINT)w - x); // clip part width
            if (sym != SYM_INSERT)
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
    if (sym == SYM_INSERT) {
        if (index > tail) index = tail;
        Expand_Series(VAL_SERIES(value), index, dup * part);

        //length in 'pixels'
        RESET_IMAGE(VAL_BIN_HEAD(value) + (index * 4), dup * part);
        Reset_Height(value);
        tail = VAL_LEN_HEAD(value);
        only = false;
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
                    ip, cast(REBYTE, arg_int), w, dupx, dupy
                );
            else
                Fill_Alpha_Line(ip, cast(REBYTE, arg_int), dup);
        }
        else if (IS_TUPLE(arg)) { // RGB
            REBYTE pixel[4];
            Set_Pixel_Tuple(pixel, arg);
            if (IS_PAIR(count)) // rectangular fill
                Fill_Rect(ip, pixel, w, dupx, dupy, only);
            else
                Fill_Line(ip, pixel, dup, only);
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

    if (sym == SYM_APPEND)
        VAL_INDEX(value) = 0;
    RETURN (value);
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
    REBYTE *ip = VAL_IMAGE_AT(value);
    REBYTE *p;
    REBINT  n;

    REBCNT len = tail - index;
    if (len == 0) {
        Init_Nulled(D_OUT);
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

    bool only; // initialization would be crossed by goto
    only = false;
    if (IS_TUPLE(arg)) {
        if (REF(only))
            only = true;
        else
            only = (VAL_TUPLE_LEN(arg) < 4);

        REBYTE pixel[4];
        Set_Pixel_Tuple(pixel, arg);
        p = Find_Color(ip, pixel, len, only);
    }
    else if (IS_INTEGER(arg)) {
        n = VAL_INT32(arg);
        if (n < 0 || n > 255) fail (Error_Out_Of_Range(arg));
        p = Find_Alpha(ip, n, len);
    }
    else if (IS_IMAGE(arg)) {
        p = nullptr;
    }
    else if (IS_BINARY(arg)) {
        p = nullptr;
    }
    else
        fail (Error_Invalid_Type(VAL_TYPE(arg)));

    if (not p) {
        Init_Nulled(D_OUT);
        return;
    }

    // Post process the search (failure or apply /match and /tail):

    Move_Value(D_OUT, value);
    assert((p - VAL_IMAGE_HEAD(value)) % 4 == 0);
    n = cast(REBCNT, (p - VAL_IMAGE_HEAD(value)) / 4);
    if (REF(match)) {
        if (n != cast(REBINT, index)) {
            Init_Nulled(D_OUT);
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
bool Image_Has_Alpha(const REBVAL *v)
{
    REBYTE *p = VAL_IMAGE_HEAD(v);

    int i = VAL_IMAGE_WIDE(v) * VAL_IMAGE_HIGH(v);
    for(; i > 0; i--, p += 4) {
        if (p[3] != 0) // non-zero (e.g. non-transparent) alpha component
            return true;
    }
    return false;
}


//
//  Copy_Rect_Data: C
//
void Copy_Rect_Data(
    REBVAL *dst,
    REBINT dx,
    REBINT dy,
    REBINT w,
    REBINT h,
    const REBVAL *src,
    REBINT sx,
    REBINT sy
){
    if (w <= 0 || h <= 0)
        return;

    // Clip at edges:
    if (dx + w > VAL_IMAGE_WIDE(dst))
        w = VAL_IMAGE_WIDE(dst) - dx;
    if (dy + h > VAL_IMAGE_HIGH(dst))
        h = VAL_IMAGE_HIGH(dst) - dy;

    const REBYTE *sbits =
        VAL_IMAGE_HEAD(src)
        + (sy * VAL_IMAGE_WIDE(src) + sx) * 4;
    REBYTE *dbits =
        VAL_IMAGE_HEAD(dst)
        + (dy * VAL_IMAGE_WIDE(dst) + dx) * 4;
    while (h--) {
        memcpy(dbits, sbits, w*4);
        sbits += VAL_IMAGE_WIDE(src) * 4;
        dbits += VAL_IMAGE_WIDE(dst) * 4;
    }
}


//
//  Make_Complemented_Image: C
//
static void Make_Complemented_Image(REBVAL *out, const REBVAL *value)
{
    REBYTE *img = VAL_IMAGE_AT(value);
    REBINT len = VAL_IMAGE_LEN_AT(value);

    Make_Image(out, VAL_IMAGE_WIDE(value), VAL_IMAGE_HIGH(value));

    REBYTE *dp = VAL_IMAGE_HEAD(out);
    for (; len > 0; len --) {
        *dp++ = ~ *img++; // copy complemented red
        *dp++ = ~ *img++; // copy complemented green
        *dp++ = ~ *img++; // copy complemented blue
        *dp++ = ~ *img++; // copy complemented alpha !!! Is this intended?
    }
}


//
//  MF_Image: C
//
void MF_Image(REB_MOLD *mo, const RELVAL *v, bool form)
{
    UNUSED(form); // no difference between MOLD and FORM at this time

    Pre_Mold(mo, v);
    if (GET_MOLD_FLAG(mo, MOLD_FLAG_ALL)) {
        DECLARE_LOCAL (head);
        Move_Value(head, KNOWN(v));
        VAL_INDEX(head) = 0; // mold all of it
        Mold_Image_Data(head, mo);
        Post_Mold(mo, v);
    }
    else {
        Append_Utf8_Codepoint(mo->series, '[');
        Mold_Image_Data(KNOWN(v), mo);
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

    REBSYM sym = VAL_WORD_SYM(verb);
    switch (sym) {

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
            return Init_Logic(D_OUT, index == 0);

        case SYM_TAIL_Q:
            return Init_Logic(D_OUT, index >= tail);

        case SYM_XY:
            return Init_Pair_Dec(
                D_OUT,
                index % VAL_IMAGE_WIDE(value),
                index / VAL_IMAGE_WIDE(value)
            );

        case SYM_INDEX:
            return Init_Integer(D_OUT, index + 1);

        case SYM_LENGTH:
            return Init_Integer(D_OUT, tail > index ? tail - index : 0);

        default:
            break;
        }

        break; }

    case SYM_COMPLEMENT:
        Make_Complemented_Image(D_OUT, value);
        return D_OUT;

    case SYM_SKIP:
    case SYM_AT:
        // This logic is somewhat complicated by the fact that INTEGER args use
        // base-1 indexing, but PAIR args use base-0.
        if (IS_PAIR(arg)) {
            if (sym == SYM_AT)
                sym = SYM_SKIP;
            diff = (VAL_PAIR_Y_INT(arg) * VAL_IMAGE_WIDE(value))
                + VAL_PAIR_X_INT(arg) + (sym == SYM_SKIP ? 0 : 1);
        } else
            diff = Get_Num_From_Arg(arg);

        index += diff;
        if (sym == SYM_SKIP) {
            if (IS_LOGIC(arg))
                --index;
        }
        else {
            if (diff > 0)
                --index; // For at, pick, poke.
        }

        if (index > tail)
            index = tail;
        else if (index < 0)
            index = 0;

        Move_Value(D_OUT, value);
        VAL_INDEX(D_OUT) = cast(REBCNT, index);
        return D_OUT;

    case SYM_CLEAR:
        FAIL_IF_READ_ONLY_SERIES(series);
        if (index < tail) {
            SET_SERIES_LEN(VAL_SERIES(value), cast(REBCNT, index));
            Reset_Height(value);
        }
        Move_Value(D_OUT, value);
        return D_OUT;

    case SYM_REMOVE: {
        FAIL_IF_READ_ONLY_SERIES(series);

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
                    fail (Error_Invalid(val));
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
        return D_OUT; }

    case SYM_APPEND:
    case SYM_INSERT:  // insert ser val /part len /only /dup count
    case SYM_CHANGE:  // change ser val /part len /only /dup count
        if (IS_NULLED_OR_BLANK(arg)) {
            if (sym == SYM_APPEND) // append returns head position
                VAL_INDEX(value) = 0;
            RETURN (value); // don't fail on read only if it would be a no-op
        }
        FAIL_IF_READ_ONLY_SERIES(series);

        return Modify_Image(frame_, verb);

    case SYM_FIND:
        Find_Image(frame_); // sets DS_OUT
        return D_OUT;

    case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;

        UNUSED(PAR(value));

        if (REF(deep))
            fail (Error_Bad_Refines_Raw());

        if (REF(types)) {
            UNUSED(ARG(kinds));
            fail (Error_Bad_Refines_Raw());
        }

        if (not REF(part)) {
            arg = value;
            goto makeCopy;
        }
        arg = ARG(limit); // can be image, integer, pair.
        if (IS_IMAGE(arg)) {
            if (VAL_SERIES(arg) != VAL_SERIES(value))
                fail (Error_Invalid(arg));
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
            Make_Image(D_OUT, w, h);
            Copy_Rect_Data(D_OUT, 0, 0, w, h, value, diff, len);
//          VAL_IMAGE_TRANSP(D_OUT) = VAL_IMAGE_TRANSP(value);
            return D_OUT;
        }
        fail (Error_Invalid_Type(VAL_TYPE(arg)));

makeCopy:
        // Src image is arg.
        len = VAL_IMAGE_LEN_AT(arg);
makeCopy2:
        Copy_Image_Value(D_OUT, arg, len);
        return D_OUT; }

    default:
        break;
    }

    fail (Error_Illegal_Action(VAL_TYPE(value), verb));
}


inline static bool Adjust_Image_Pick_Index_Is_Valid(
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
        fail (Error_Invalid(picker));

    *index += n;
    if (n > 0)
        (*index)--;

    if (n == 0 || *index < 0 || *index >= cast(REBINT, VAL_LEN_HEAD(value)))
        return false; // out of range

    return true;
}


//
//  Pick_Image: C
//
void Pick_Image(REBVAL *out, const REBVAL *value, const REBVAL *picker)
{
    REBINT index = cast(REBINT, VAL_INDEX(value));
    REBINT len = VAL_LEN_HEAD(value) - index;
    len = MAX(len, 0);

    REBYTE *src = VAL_IMAGE_AT(value);

    if (IS_WORD(picker)) {
        switch (VAL_WORD_SYM(picker)) {
        case SYM_SIZE:
            Init_Pair_Dec(
                out,
                VAL_IMAGE_WIDE(value),
                VAL_IMAGE_HIGH(value)
            );
            break;

        case SYM_RGB: {
            REBSER *nser = Make_Binary(len * 3);
            SET_SERIES_LEN(nser, len * 3);
            RGB_To_Bin(BIN_HEAD(nser), src, len, false);
            Init_Binary(out, nser);
            break; }

        case SYM_ALPHA: {
            REBSER *nser = Make_Binary(len);
            SET_SERIES_LEN(nser, len);
            Alpha_To_Bin(BIN_HEAD(nser), src, len);
            Init_Binary(out, nser);
            break; }

        default:
            fail (Error_Invalid(picker));
        }
        return;
    }

    if (Adjust_Image_Pick_Index_Is_Valid(&index, value, picker))
        Init_Tuple_From_Pixel(out, VAL_IMAGE_AT_HEAD(value, index));
    else
        Init_Nulled(out);
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

    REBYTE *src = VAL_IMAGE_AT(value);

    if (IS_WORD(picker)) {
        switch (VAL_WORD_SYM(picker)) {
        case SYM_SIZE:
            if (!IS_PAIR(poke) || !VAL_PAIR_X_DEC(poke))
                fail (Error_Invalid(poke));

            VAL_IMAGE_WIDE(value) = VAL_PAIR_X_INT(poke);
            VAL_IMAGE_HIGH(value) = MIN(
                VAL_PAIR_Y_INT(poke),
                cast(REBINT, VAL_LEN_HEAD(value) / VAL_PAIR_X_INT(poke))
            );
            break;

        case SYM_RGB:
            if (IS_TUPLE(poke)) {
                REBYTE pixel[4];
                Set_Pixel_Tuple(pixel, poke);
                Fill_Line(src, pixel, len, true);
            }
            else if (IS_INTEGER(poke)) {
                REBINT byte = VAL_INT32(poke);
                if (byte < 0 || byte > 255)
                    fail (Error_Out_Of_Range(poke));

                REBYTE pixel[4];
                pixel[0] = byte; // red
                pixel[1] = byte; // green
                pixel[2] = byte; // blue
                pixel[3] = 0xFF; // opaque alpha
                Fill_Line(src, pixel, len, true);
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
                fail (Error_Invalid(poke));
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
                fail (Error_Invalid(poke));
            break;

        default:
            fail (Error_Invalid(picker));
        }
        return;
    }

    if (!Adjust_Image_Pick_Index_Is_Valid(&index, value, picker))
        fail (Error_Out_Of_Range(picker));

    if (IS_TUPLE(poke)) { // set whole pixel
        Set_Pixel_Tuple(VAL_IMAGE_AT_HEAD(value, index), poke);
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

    REBYTE *dp = VAL_IMAGE_AT_HEAD(value, index);
    dp[3] = alpha;
}


//
//  PD_Image: C
//
REB_R PD_Image(
    REBPVS *pvs,
    const REBVAL *picker,
    const REBVAL *opt_setval
){
    if (opt_setval != NULL) {
        Poke_Image_Fail_If_Read_Only(pvs->out, picker, opt_setval);
        return R_INVISIBLE;
    }

    Pick_Image(pvs->out, pvs->out, picker);
    return pvs->out;
}
