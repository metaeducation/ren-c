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
// Copyright 2012-2019 Ren-C Open Source Contributors
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
// See notes in %extensions/image/README.md

#include "sys-core.h"

#include "sys-image.h"


//
//  Tuples_To_RGBA: C
//
void Tuples_To_RGBA(REBYTE *rgba, REBLEN size, const REBVAL *head, REBLEN len)
{
    if (len > size) len = size; // avoid over-run

    for (; len > 0; len--, rgba += 4, ++head) {
        const REBYTE *bin = VAL_TUPLE(head);
        rgba[0] = bin[0];
        rgba[1] = bin[1];
        rgba[2] = bin[2];
        rgba[3] = bin[3];
    }
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
//  Array_Has_Non_Tuple: C
//
// Checks the given ANY-ARRAY! REBVAL from its current index position to
// the end to see if any of its contents are not TUPLE!.  If so, returns
// true and `index_out` will contain the index position from the head of
// the array of the non-tuple.  Otherwise returns false.
//
bool Array_Has_Non_Tuple(REBLEN *index_out, const RELVAL *blk)
{
    assert(ANY_ARRAY(blk));

    REBLEN len = VAL_LEN_HEAD(blk);
    *index_out = VAL_INDEX(blk);
    for (; *index_out < len; (*index_out)++)
        if (not IS_TUPLE(VAL_ARRAY_AT_HEAD(blk, *index_out)))
            return true;

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
    if (dx + w > VAL_IMAGE_WIDTH(dst))
        w = VAL_IMAGE_WIDTH(dst) - dx;
    if (dy + h > VAL_IMAGE_HEIGHT(dst))
        h = VAL_IMAGE_HEIGHT(dst) - dy;

    const REBYTE *sbits =
        VAL_IMAGE_HEAD(src)
        + (sy * VAL_IMAGE_WIDTH(src) + sx) * 4;
    REBYTE *dbits =
        VAL_IMAGE_HEAD(dst)
        + (dy * VAL_IMAGE_WIDTH(dst) + dx) * 4;
    while (h--) {
        memcpy(dbits, sbits, w*4);
        sbits += VAL_IMAGE_WIDTH(src) * 4;
        dbits += VAL_IMAGE_WIDTH(dst) * 4;
    }
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
//  Fill_Line: C
//
void Fill_Line(REBYTE *ip, const REBYTE pixel[4], REBLEN len, bool only)
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
    REBLEN w,
    REBINT dupx,
    REBINT dupy,
    bool only
){
    for (; dupy > 0; dupy--, ip += (w * 4))
        Fill_Line(ip, pixel, dupx, only);
}


//
//  CT_Image: C
//
REBINT CT_Image(REBCEL(const*) a, REBCEL(const*) b, bool strict)
{
    UNUSED(strict);

    if (VAL_IMAGE_WIDTH(a) != VAL_IMAGE_WIDTH(b))
        return VAL_IMAGE_WIDTH(a) > VAL_IMAGE_WIDTH(b) ? 1 : -1;

    if (VAL_IMAGE_HEIGHT(a) != VAL_IMAGE_HEIGHT(b))
        return VAL_IMAGE_HEIGHT(a) > VAL_IMAGE_HEIGHT(b) ? 1 : -1;

    // !!! There is an image "position" stored in the binary.  This is a
    // dodgy concept of a linear index into the image being an X/Y
    // coordinate and permitting "series" operations.  In any case, for
    // two images to compare alike they are compared according to this...
    // but note the width and height aren't taken into account.
    //
    // https://github.com/rebol/rebol-issues/issues/801
    //
    if (VAL_IMAGE_POS(a) != VAL_IMAGE_POS(b))
        return VAL_IMAGE_POS(a) > VAL_IMAGE_POS(b) ? 1 : -1;

    assert(VAL_IMAGE_LEN_AT(a) == VAL_IMAGE_LEN_AT(b));

    int cmp = memcmp(VAL_IMAGE_AT(a), VAL_IMAGE_AT(b), VAL_IMAGE_LEN_AT(a));
    return cmp;
}


void Copy_Image_Value(REBVAL *out, const REBVAL *arg, REBINT len)
{
    len = MAX(len, 0); // no negatives
    len = MIN(len, cast(REBINT, VAL_IMAGE_LEN_AT(arg)));

    REBINT w = VAL_IMAGE_WIDTH(arg);
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

    Init_Image_Black_Opaque(out, w, h);
    memcpy(VAL_IMAGE_HEAD(out), VAL_IMAGE_AT(arg), w * h * 4);
}


//
//  MAKE_Image: C
//
REB_R MAKE_Image(
    REBVAL *out,
    enum Reb_Kind kind,
    const REBVAL *opt_parent,
    const REBVAL *arg
){
    if (opt_parent)
        fail (Error_Bad_Make_Parent(kind, opt_parent));

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
        Init_Image_Black_Opaque(out, 0, 0);
    }
    else if (IS_PAIR(arg)) {  // `make image! 10x20`
        REBINT w = VAL_PAIR_X_INT(arg);
        REBINT h = VAL_PAIR_Y_INT(arg);
        w = MAX(w, 0);
        h = MAX(h, 0);
        Init_Image_Black_Opaque(out, w, h);
    }
    else if (IS_BLOCK(arg)) {  // make image! [size rgba index]
        const RELVAL *item = VAL_ARRAY_AT(arg);
        if (not IS_PAIR(item))
            goto bad_make;

        REBINT w = VAL_PAIR_X_INT(item);
        REBINT h = VAL_PAIR_Y_INT(item);
        if (w < 0 or h < 0)
            goto bad_make;

        ++item;

        if (IS_END(item)) {  // was just `make image! [10x20]`, allow it
            Init_Image_Black_Opaque(out, w, h);
            ++item;
        }
        else if (IS_BINARY(item)) {  // use bytes as-is

            // !!! R3-Alpha separated out the alpha channel from the RGB
            // data in MAKE, even though it stored all the data together.
            // We can't use a binary directly as the backing store for an
            // image unless it has all the RGBA components together.  While
            // some MAKE-like procedure might allow you to pass in separate
            // components, the value of a system one is to use the data
            // directly as-is...so Ren-C only supports RGBA.

            if (VAL_INDEX(item) != 0)
                fail ("MAKE IMAGE! w/BINARY! must have binary at HEAD");

            if (VAL_LEN_HEAD(item) != cast(REBLEN, w * h * 4))
                fail ("MAKE IMAGE! w/BINARY! must have RGBA pixels for size");

            Init_Image(out, VAL_BINARY(item), w, h);
            ++item;

            // !!! Sketchy R3-Alpha concept: "image position".  The block
            // MAKE IMAGE! format allowed you to specify it.

            if (NOT_END(item) and IS_INTEGER(item)) {
                VAL_IMAGE_POS(out) = (Int32s(SPECIFIC(item), 1) - 1);
                ++item;
            }
        }
        else if (IS_TUPLE(item)) {  // `make image! [1.2.3.255 4.5.6.128 ...]`
            Init_Image_Black_Opaque(out, w, h);  // inefficient, overwritten
            REBYTE *ip = VAL_IMAGE_HEAD(out); // image pointer

            REBYTE pixel[4];
            Set_Pixel_Tuple(pixel, item);
            Fill_Rect(ip, pixel, w, w, h, true);
            ++item;
            if (IS_INTEGER(item)) {
                Fill_Alpha_Rect(
                    ip, cast(REBYTE, VAL_INT32(item)), w, w, h
                );
                ++item;
            }
        }
        else if (IS_BLOCK(item)) {
            Init_Image_Black_Opaque(out, w, h);  // inefficient, overwritten

            REBLEN bad_index;
            if (Array_Has_Non_Tuple(&bad_index, item)) {
                REBSPC *derived = Derive_Specifier(VAL_SPECIFIER(arg), item);
                fail (Error_Bad_Value_Core(
                    VAL_ARRAY_AT_HEAD(item, bad_index),
                    derived
                ));
            }

            REBYTE *ip = VAL_IMAGE_HEAD(out);  // image pointer
            Tuples_To_RGBA(
                ip, w * h, SPECIFIC(VAL_ARRAY_AT(item)), VAL_LEN_AT(item)
            );
        }
        else
            goto bad_make;

        if (NOT_END(item))
            fail ("Too many elements in BLOCK! for MAKE IMAGE!");
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
    assert(kind == REB_CUSTOM);
    UNUSED(kind);
    UNUSED(out);

    fail (arg);
}


//
//  Reset_Height: C
//
// Set height based on tail and width.
//
void Reset_Height(REBVAL *value)
{
    REBLEN w = VAL_IMAGE_WIDTH(value);
    VAL_IMAGE_HEIGHT(value) = w ? (VAL_LEN_HEAD(value) / w) : 0;
}


//
//  Init_Tuple_From_Pixel: C
//
REBVAL *Init_Tuple_From_Pixel(RELVAL *out, const REBYTE *dp)
{
    return Init_Tuple(out, dp, 4);
}


//
//  Find_Color: C
//
REBYTE *Find_Color(
    REBYTE *ip,
    const REBYTE pixel[4],
    REBLEN len,
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
REBYTE *Find_Alpha(REBYTE *ip, REBYTE alpha, REBLEN len)
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
void Bin_To_RGB(REBYTE *rgba, REBLEN size, const REBYTE *bin, REBLEN len)
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
    REBLEN size,
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
void Alpha_To_Bin(REBYTE *bin, const REBYTE *rgba, REBINT len)
{
    for (; len > 0; len--, rgba += 4)
        *bin++ = rgba[3];
}


//
//  Bin_To_Alpha: C
//
void Bin_To_Alpha(REBYTE *rgba, REBLEN size, const REBYTE *bin, REBINT len)
{
    if (len > (REBINT)size) len = size; // avoid over-run

    for (; len > 0; len--, rgba += 4)
        rgba[3] = *bin++;
}


//
//  Mold_Image_Data: C
//
// Output RGBA image data
//
// !!! R3-Alpha always used 4 bytes per pixel for images, so the idea that
// images would "not have an alpha channel" only meant that they had all
// transparent bytes.  In order to make images less monolithic (and enable
// them to be excised from the core into an extension), the image builds
// directly on a BINARY! that the user can pass in and extract.  This has
// to be consistent with the internal format, so the idea of "alpha-less"
// images is removed from MAKE IMAGE! and related molding.
//
void Mold_Image_Data(REB_MOLD *mo, REBCEL(const*) value)
{
    REBLEN num_pixels = VAL_IMAGE_LEN_AT(value); // # from index to tail
    const REBYTE *rgba = VAL_IMAGE_AT(value);

    Append_Int(mo->series, VAL_IMAGE_WIDTH(value));
    Append_Ascii(mo->series, "x");
    Append_Int(mo->series, VAL_IMAGE_HEIGHT(value));

    Append_Ascii(mo->series, " #{");

    REBLEN i;
    for (i = 0; i < num_pixels; ++i, rgba += 4) {
        if ((i % 10) == 0)
            Append_Codepoint(mo->series, LF);
        Form_RGBA(mo, rgba);
    }

    Append_Ascii(mo->series, "\n}");
}


//
//  Clear_Image: C
//
// Clear image data (sets R=G=B=A to 0)
//
void Clear_Image(REBVAL *img)
{
    REBLEN w = VAL_IMAGE_WIDTH(img);
    REBLEN h = VAL_IMAGE_HEIGHT(img);
    REBYTE *p = VAL_IMAGE_HEAD(img);
    memset(p, 0, w * h * 4);
}


//
//  Modify_Image: C
//
// CHANGE/INSERT/APPEND image
//
// !!! R3-Alpha had the concept that images were an "ANY-SERIES!", which was
// slippery.  What does it mean to "append" a red pixel to a 10x10 image?
// What about to "insert"?  CHANGE may seem to make sense in a positional
// world where the position was a coordinate and you change to a rectangle
// of data that is another image.
//
// While the decode/encode abilities of IMAGE! are preserved, R3-Alpha code
// like this has been excised from the core and into an extension for a
// reason.  (!)  The code is deprecated, but kept around and building for any
// sufficiently motivated individual who wanted to review it.
//
REB_R Modify_Image(REBFRM *frame_, const REBVAL *verb)
{
    INCLUDE_PARAMS_OF_CHANGE;  // currently must have same frame as CHANGE

    if (REF(line))
        fail (Error_Bad_Refines_Raw());

    REBVAL *value = ARG(series);  // !!! confusing name
    REBVAL *arg = ARG(value);

    REBSER *bin = VAL_BINARY_ENSURE_MUTABLE(VAL_IMAGE_BIN(value));

    REBLEN index = VAL_IMAGE_POS(value);
    REBLEN tail = VAL_IMAGE_LEN_HEAD(value);
    REBLEN n;
    REBYTE *ip;

    REBINT w = VAL_IMAGE_WIDTH(value);
    if (w == 0)
        RETURN (value);

    REBSYM sym = VAL_WORD_SYM(verb);
    if (sym == SYM_APPEND) {
        index = tail;
        sym = SYM_INSERT;
    }

    REBINT x = index % w;  // offset on the line
    REBINT y = index / w;  // offset line

    bool only = did REF(only);

    // Validate that block arg is all tuple values:
    if (IS_BLOCK(arg) && Array_Has_Non_Tuple(&n, arg))
        fail (Error_Bad_Value_Core(
            VAL_ARRAY_AT_HEAD(arg, n), VAL_SPECIFIER(arg)
        ));

    REBINT dup = 1;
    REBINT dup_x = 0;
    REBINT dup_y = 0;

    if (REF(dup)) {  // "it specifies fill size"
        if (IS_INTEGER(ARG(dup))) {
            dup = VAL_INT32(ARG(dup));
            dup = MAX(dup, 0);
            if (dup == 0)
                RETURN (value);
        }
        else if (IS_PAIR(ARG(dup))) {  // rectangular dup
            dup_x = VAL_PAIR_X_INT(ARG(dup));
            dup_y = VAL_PAIR_Y_INT(ARG(dup));
            dup_x = MAX(dup_x, 0);
            dup_x = MIN(dup_x, cast(REBINT, w) - x);  // clip dup width
            dup_y = MAX(dup_y, 0);
            if (sym != SYM_INSERT)
                dup_y = MIN(dup_y, cast(REBINT, VAL_IMAGE_HEIGHT(value)) - y);
            else
                dup = dup_y * w;
            if (dup_x == 0 or dup_y == 0)
                RETURN (value);
        }
        else
            fail (Error_Invalid_Type(VAL_TYPE(ARG(dup))));
    }

    REBINT part = 1;
    REBINT part_x = 0;
    REBINT part_y = 0;

    if (REF(part)) {  // only allowed when arg is a series
        if (IS_BINARY(arg)) {
            if (IS_INTEGER(ARG(part))) {
                part = VAL_INT32(ARG(part));
            } else if (IS_BINARY(ARG(part))) {
                part = (VAL_INDEX(ARG(part)) - VAL_INDEX(arg)) / 4;
            } else
                fail (PAR(part));
            part = MAX(part, 0);
        }
        else if (IS_IMAGE(arg)) {
            if (IS_INTEGER(ARG(part))) {
                part = VAL_INT32(ARG(part));
                part = MAX(part, 0);
            }
            else if (IS_IMAGE(ARG(part))) {
                if (VAL_IMAGE_WIDTH(ARG(part)) == 0)
                    fail (PAR(part));

                part_x = VAL_IMAGE_POS(ARG(part)) - VAL_IMAGE_POS(arg);
                part_y = part_x / VAL_IMAGE_WIDTH(ARG(part));
                part_y = MAX(part_y, 1);
                part_x = MIN(part_x, cast(REBINT, VAL_IMAGE_WIDTH(arg)));
                goto len_compute;
            }
            else if (IS_PAIR(ARG(part))) {
                part_x = VAL_PAIR_X_INT(ARG(part));
                part_y = VAL_PAIR_Y_INT(ARG(part));
            len_compute:
                part_x = MAX(part_x, 0);
                part_x = MIN(part_x, cast(REBINT, w) - x);  // clip part width
                part_y = MAX(part_y, 0);
                if (sym != SYM_INSERT)
                    part_y = MIN(
                        part_y,
                        cast(REBINT, VAL_IMAGE_HEIGHT(value) - y)
                    );
                else
                    part = part_y * w;
                if (part_x == 0 || part_y == 0)
                    RETURN (value);
            }
            else
                fail (Error_Invalid_Type(VAL_TYPE(ARG(part))));
        }
        else
            fail (arg);  // /PART not allowed
    }
    else {
        if (IS_IMAGE(arg)) {  // Use image for /PART sizes
            part_x = VAL_IMAGE_WIDTH(arg);
            part_y = VAL_IMAGE_HEIGHT(arg);
            part_x = MIN(part_x, cast(REBINT, w) - x);  // clip part width
            if (sym != SYM_INSERT)
                part_y = MIN(part_y, cast(REBINT, VAL_IMAGE_HEIGHT(value)) - y);
            else
                part = part_y * w;
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
        Expand_Series(bin, index, dup * part);

        //length in 'pixels'
        RESET_IMAGE(BIN_HEAD(bin) + (index * 4), dup * part);
        Reset_Height(value);
        tail = VAL_LEN_HEAD(value);
        only = false;
    }
    ip = VAL_IMAGE_HEAD(value);

    // Handle the datatype of the argument.
    if (IS_INTEGER(arg) || IS_TUPLE(arg)) {  // scalars
        if (index + dup > tail) dup = tail - index;  // clip it
        ip += index * 4;
        if (IS_INTEGER(arg)) { // Alpha channel
            REBINT arg_int = VAL_INT32(arg);
            if ((arg_int < 0) || (arg_int > 255))
                fail (Error_Out_Of_Range(arg));

            if (IS_PAIR(ARG(dup)))  // rectangular fill
                Fill_Alpha_Rect(
                    ip, cast(REBYTE, arg_int), w, dup_x, dup_y
                );
            else
                Fill_Alpha_Line(ip, cast(REBYTE, arg_int), dup);
        }
        else if (IS_TUPLE(arg)) {  // RGB
            REBYTE pixel[4];
            Set_Pixel_Tuple(pixel, arg);
            if (IS_PAIR(ARG(dup)))  // rectangular fill
                Fill_Rect(ip, pixel, w, dup_x, dup_y, only);
            else
                Fill_Line(ip, pixel, dup, only);
        }
    } else if (IS_IMAGE(arg)) {
        // dst dx dy w h src sx sy
        Copy_Rect_Data(value, x, y, part_x, part_y, arg, 0, 0);
    }
    else if (IS_BINARY(arg)) {
        if (index + part > tail) part = tail - index;  // clip it
        ip += index * 4;
        for (; dup > 0; dup--, ip += part * 4)
            Bin_To_RGBA(ip, part, VAL_BIN_AT(arg), part, only);
    }
    else if (IS_BLOCK(arg)) {
        if (index + part > tail) part = tail - index;  // clip it
        ip += index * 4;
        for (; dup > 0; dup--, ip += part * 4)
            Tuples_To_RGBA(ip, part, SPECIFIC(VAL_ARRAY_AT(arg)), part);
    }
    else
        fail (Error_Invalid_Type(VAL_TYPE(arg)));

    Reset_Height(value);

    if (sym == SYM_APPEND)
        VAL_IMAGE_POS(value) = 0;
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

    UNUSED(REF(reverse));  // Deprecated https://forum.rebol.info/t/1126
    UNUSED(REF(last));  // ...a HIJACK in %mezz-legacy errors if used

    REBVAL *value = ARG(series);
    REBVAL *arg = ARG(pattern);
    REBLEN index = VAL_IMAGE_POS(arg);
    REBLEN tail = VAL_IMAGE_LEN_HEAD(arg);
    REBYTE *ip = VAL_IMAGE_AT(arg);

    REBLEN len = tail - index;
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
        || REF(match)
        || REF(part)
    ){
        fail (Error_Bad_Refines_Raw());
    }

    bool only = false;

    REBYTE *p;

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
        REBINT i = VAL_INT32(arg);
        if (i < 0 or i > 255)
            fail (Error_Out_Of_Range(arg));

        p = Find_Alpha(ip, i, len);
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

    REBINT n = cast(REBLEN, (p - VAL_IMAGE_HEAD(value)) / 4);
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

    VAL_IMAGE_POS(value) = n;
    return;
}


//
//  Image_Has_Alpha: C
//
// !!! See code in R3-Alpha for VITT_ALPHA and the `save` flag.
//
bool Image_Has_Alpha(REBCEL(const*) v)
{
    REBYTE *p = VAL_IMAGE_HEAD(v);

    int i = VAL_IMAGE_WIDTH(v) * VAL_IMAGE_HEIGHT(v);
    for(; i > 0; i--, p += 4) {
        if (p[3] != 0) // non-zero (e.g. non-transparent) alpha component
            return true;
    }
    return false;
}


//
//  Make_Complemented_Image: C
//
static void Make_Complemented_Image(REBVAL *out, const REBVAL *v)
{
    REBYTE *img = VAL_IMAGE_AT(v);
    REBINT len = VAL_IMAGE_LEN_AT(v);

    Init_Image_Black_Opaque(out, VAL_IMAGE_WIDTH(v), VAL_IMAGE_HEIGHT(v));

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
void MF_Image(REB_MOLD *mo, REBCEL(const*) v, bool form)
{
    UNUSED(form); // no difference between MOLD and FORM at this time

    Pre_Mold(mo, v);
    if (GET_MOLD_FLAG(mo, MOLD_FLAG_ALL)) {
        DECLARE_LOCAL (head);
        Move_Value(head, SPECIFIC(CELL_TO_VAL(v)));
        VAL_IMAGE_POS(head) = 0; // mold all of it
        Mold_Image_Data(mo, head);
        Post_Mold(mo, v);
    }
    else {
        Append_Codepoint(mo->series, '[');
        Mold_Image_Data(mo, v);
        Append_Codepoint(mo->series, ']');
        End_Mold(mo);
    }
}


//
//  REBTYPE: C
//
REBTYPE(Image)
{
    REBVAL  *value = D_ARG(1);
    REBINT  diff, len, w, h;
    REBVAL  *val;

    REBINT index = VAL_IMAGE_POS(value);
    REBINT tail = cast(REBINT, BIN_LEN(VAL_BINARY(VAL_IMAGE_BIN(value))));

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
            VAL_IMAGE_POS(value) = 0;
            break;

        case SYM_TAIL:
            VAL_IMAGE_POS(value) = cast(REBLEN, tail);
            break;

        case SYM_HEAD_Q:
            return Init_Logic(D_OUT, index == 0);

        case SYM_TAIL_Q:
            return Init_Logic(D_OUT, index >= tail);

        case SYM_XY:
            return Init_Pair_Int(
                D_OUT,
                index % VAL_IMAGE_WIDTH(value),
                index / VAL_IMAGE_WIDTH(value)
            );

        case SYM_INDEX:
            return Init_Integer(D_OUT, index + 1);

        case SYM_LENGTH:
            return Init_Integer(D_OUT, tail > index ? tail - index : 0);

        case SYM_BYTES: {
            //
            // !!! The BINARY! currently has a position in it.  This notion
            // of images being at an "index" is sketchy.  Assume that someone
            // asking for the bytes doesn't care about the index.
            //
            const REBBIN *bin = VAL_BINARY(VAL_IMAGE_BIN(value));
            return Init_Binary(D_OUT, bin); }  // at 0 index

        default:
            break;
        }

        break; }

    case SYM_COMPLEMENT:
        Make_Complemented_Image(D_OUT, value);
        return D_OUT;

    case SYM_SKIP:
    case SYM_AT: {
        REBVAL *arg = D_ARG(2);

        // This logic is somewhat complicated by the fact that INTEGER args use
        // base-1 indexing, but PAIR args use base-0.
        if (IS_PAIR(arg)) {
            if (sym == SYM_AT)
                sym = SYM_SKIP;
            diff = (VAL_PAIR_Y_INT(arg) * VAL_IMAGE_WIDTH(value))
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

        VAL_IMAGE_POS(value) = cast(REBLEN, index);
        RETURN (value); }

    case SYM_CLEAR:
        if (index < tail) {
            SET_SERIES_LEN(
                VAL_BINARY_ENSURE_MUTABLE(VAL_IMAGE_BIN(value)),
                cast(REBLEN, index)
            );
            Reset_Height(value);
        }
        RETURN (value);


    case SYM_REMOVE: {
        INCLUDE_PARAMS_OF_REMOVE;
        UNUSED(PAR(series));

        REBSER *series = VAL_BINARY_ENSURE_MUTABLE(VAL_IMAGE_BIN(value));

        if (REF(part)) {
            val = ARG(part);
            if (IS_INTEGER(val)) {
                len = VAL_INT32(val);
            }
            else if (IS_IMAGE(val)) {
                if (!VAL_IMAGE_WIDTH(val))
                    fail (val);
                len = VAL_IMAGE_POS(val) - VAL_IMAGE_POS(value);
            }
            else
                fail (Error_Invalid_Type(VAL_TYPE(val)));
        }
        else len = 1;

        index = cast(REBINT, VAL_IMAGE_POS(value));
        if (index < tail && len != 0) {
            Remove_Series_Units(series, VAL_INDEX(value), len);
        }
        Reset_Height(value);
        RETURN (value); }

    case SYM_APPEND:
    case SYM_INSERT:   // insert ser val /part len /only /dup count
    case SYM_CHANGE: { // change ser val /part len /only /dup count
        if (IS_NULLED_OR_BLANK(D_ARG(2))) {
            if (sym == SYM_APPEND) // append returns head position
                VAL_IMAGE_POS(value) = 0;
            RETURN (value); // don't fail on read only if it would be a no-op
        }
        ENSURE_MUTABLE(value);

        return Modify_Image(frame_, verb); }

    case SYM_FIND:
        Find_Image(frame_); // sets DS_OUT
        return D_OUT;

    case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;

        UNUSED(PAR(value));

        if (REF(deep))
            fail (Error_Bad_Refines_Raw());

        if (REF(types))
            fail (Error_Bad_Refines_Raw());

        REBVAL *arg;
        if (not REF(part)) {
            arg = value;
            goto makeCopy;
        }
        arg = ARG(part); // can be image, integer, pair.
        if (IS_IMAGE(arg)) {
            if (VAL_IMAGE_BIN(arg) != VAL_IMAGE_BIN(value))
                fail (arg);
            len = VAL_IMAGE_POS(arg) - VAL_IMAGE_POS(value);
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
            diff = MIN(VAL_LEN_HEAD(value), VAL_IMAGE_POS(value));
            diff = MAX(0, diff);
            index = VAL_IMAGE_WIDTH(value); // width
            if (index) {
                len = diff / index; // compute y offset
                diff %= index; // compute x offset
            } else len = diff = 0; // avoid div zero
            w = MIN(w, index - diff); // img-width - x-pos
            h = MIN(h, (int)(VAL_IMAGE_HEIGHT(value) - len)); // img-high - y-pos
            Init_Image_Black_Opaque(D_OUT, w, h);
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

    return R_UNHANDLED;
}


inline static bool Adjust_Image_Pick_Index_Is_Valid(
    REBINT *index, // gets adjusted
    const REBVAL *value, // image
    const RELVAL *picker
) {
    REBINT n;
    if (IS_PAIR(picker)) {
        n = (
            (VAL_PAIR_Y_INT(picker) - 1) * VAL_IMAGE_WIDTH(value)
            + (VAL_PAIR_X_INT(picker) - 1)
        ) + 1;
    }
    else if (IS_INTEGER(picker))
        n = VAL_INT32(picker);
    else if (IS_DECIMAL(picker))
        n = cast(REBINT, VAL_DECIMAL(picker));
    else if (IS_LOGIC(picker))
        n = VAL_LOGIC(picker) ? 1 : 2;
    else
        fail (rebUnrelativize(picker));

    *index += n;
    if (n > 0)
        (*index)--;

    if (
        n == 0
        or *index < 0
        or *index >= cast(REBINT, VAL_IMAGE_LEN_HEAD(value))
    ){
        return false; // out of range
    }

    return true;
}


//
//  Pick_Image: C
//
void Pick_Image(REBVAL *out, const REBVAL *value, const RELVAL *picker)
{
    REBINT index = cast(REBINT, VAL_IMAGE_POS(value));
    REBINT len = VAL_IMAGE_LEN_HEAD(value) - index;
    len = MAX(len, 0);

    REBYTE *src = VAL_IMAGE_AT(value);

    if (IS_WORD(picker)) {
        switch (VAL_WORD_SYM(picker)) {
        case SYM_SIZE:
            Init_Pair_Int(
                out,
                VAL_IMAGE_WIDTH(value),
                VAL_IMAGE_HEIGHT(value)
            );
            break;

        case SYM_RGB: {
            REBSER *nser = Make_Binary(len * 3);
            SET_SERIES_LEN(nser, len * 3);
            RGB_To_Bin(BIN_HEAD(nser), src, len, false);
            TERM_SERIES(nser);
            Init_Binary(out, nser);
            break; }

        case SYM_ALPHA: {
            REBSER *nser = Make_Binary(len);
            SET_SERIES_LEN(nser, len);
            Alpha_To_Bin(BIN_HEAD(nser), src, len);
            TERM_SERIES(nser);
            Init_Binary(out, nser);
            break; }

        default:
            fail (rebUnrelativize(picker));
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
    const RELVAL *picker,
    const REBVAL *poke
){
    ENSURE_MUTABLE(value);

    REBINT index = cast(REBINT, VAL_IMAGE_POS(value));
    REBINT len = VAL_IMAGE_LEN_HEAD(value) - index;
    len = MAX(len, 0);

    REBYTE *src = VAL_IMAGE_AT(value);

    if (IS_WORD(picker)) {
        switch (VAL_WORD_SYM(picker)) {
        case SYM_SIZE:
            if (!IS_PAIR(poke) || !VAL_PAIR_X_DEC(poke))
                fail (poke);

            VAL_IMAGE_WIDTH(value) = VAL_PAIR_X_INT(poke);
            VAL_IMAGE_HEIGHT(value) = MIN(
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
            fail (rebUnrelativize(picker));
        }
        return;
    }

    if (!Adjust_Image_Pick_Index_Is_Valid(&index, value, picker))
        fail (Error_Out_Of_Range(SPECIFIC(picker)));

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
    const RELVAL *picker,
    const REBVAL *opt_setval
){
    if (opt_setval != NULL) {
        Poke_Image_Fail_If_Read_Only(pvs->out, picker, opt_setval);
        return R_INVISIBLE;
    }

    Pick_Image(pvs->out, pvs->out, picker);
    return pvs->out;
}
