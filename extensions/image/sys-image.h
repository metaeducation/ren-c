//
//  File: %sys-image.h
//  Summary: {Definitions for IMAGE! Datatype}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
//=////////////////////////////////////////////////////////////////////////=//
//
// See %extensions/image/README.md
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * The optimization of using the LINK() and MISC() fields of a BINARY! is
//   not used in Ren-C's image, because that would preclude the use of a
//   binary from another source who needed those fields for some other form
//   of tracking.  (Imagine if vector used MISC() for its signed flag, and
//   you tried to `make image! bytes of my-vector`, overwriting the flag
//   with the image width.)  Instead, a singular array to hold the binary
//   is made.  A `make image!` that did not use a foreign source could
//   optimize this and consider it the binary owner, at same cost as R3-Alpha.

extern REBTYP* EG_Image_Type;

inline static REBVAL *VAL_IMAGE_BIN(const REBCEL *v) {
    assert(CELL_CUSTOM_TYPE(v) == EG_Image_Type);
    return KNOWN(ARR_SINGLE(ARR(PAYLOAD(Any, v).first.node)));
}

#define VAL_IMAGE_WIDTH(v) \
    LINK(ARR(PAYLOAD(Any, v).first.node)).custom.i

#define VAL_IMAGE_HEIGHT(v) \
    MISC(ARR(PAYLOAD(Any, v).first.node)).custom.i

inline static REBYTE *VAL_IMAGE_HEAD(const REBCEL *v) {
    assert(CELL_CUSTOM_TYPE(v) == EG_Image_Type);
    return SER_DATA_RAW(VAL_BINARY(VAL_IMAGE_BIN(v)));
}

inline static REBYTE *VAL_IMAGE_AT_HEAD(const REBCEL *v, REBCNT pos) {
    return VAL_IMAGE_HEAD(v) + (pos * 4);
}


// !!! The functions that take into account the current index position in the
// IMAGE!'s ANY-SERIES! payload are sketchy, in the sense that being offset
// into the data does not change the width or height...only the length when
// viewing the image as a 1-dimensional series.  This is not likely to make
// a lot of sense.

#define VAL_IMAGE_POS(v) \
    VAL_INDEX(VAL_IMAGE_BIN(v))

inline static REBYTE *VAL_IMAGE_AT(const REBCEL *v) {
    return VAL_IMAGE_AT_HEAD(v, VAL_IMAGE_POS(v));
}

inline static REBCNT VAL_IMAGE_LEN_HEAD(const REBCEL *v) {
    return VAL_IMAGE_HEIGHT(v) * VAL_IMAGE_WIDTH(v);
}

inline static REBCNT VAL_IMAGE_LEN_AT(const REBCEL *v) {
    if (VAL_IMAGE_POS(v) >= VAL_IMAGE_LEN_HEAD(v))
        return 0;  // avoid negative position
    return VAL_IMAGE_LEN_HEAD(v) - VAL_IMAGE_POS(v);
}

inline static bool IS_IMAGE(const RELVAL *v) {
    //
    // Note that for this test, if there's a quote level it doesn't count...
    // that would be QUOTED! (IS_QUOTED()).  To test for quoted images, you
    // have to call CELL_CUSTOM_TYPE() on the VAL_UNESCAPED() cell.
    //
    return IS_CUSTOM(v) and CELL_CUSTOM_TYPE(v) == EG_Image_Type;
}

inline static REBVAL *Init_Image(
    RELVAL *out,
    REBSER *bin,
    REBCNT width,
    REBCNT height
){
    assert(GET_SERIES_FLAG(bin, MANAGED));

    REBARR *a = Alloc_Singular(NODE_FLAG_MANAGED);
    Init_Binary(ARR_SINGLE(a), bin);
    LINK(a).custom.i = width;  // see notes on why this isn't put on bin...
    MISC(a).custom.i = height;  // (...it would corrupt shared series!)

    RESET_CUSTOM_CELL(out, EG_Image_Type, CELL_FLAG_FIRST_IS_NODE);
    INIT_VAL_NODE(out, a);

    assert(VAL_IMAGE_POS(out) == 0);  // !!! sketchy concept, is in BINARY!

    return KNOWN(out);
}

inline static void RESET_IMAGE(REBYTE *p, REBCNT num_pixels) {
    REBYTE *start = p;
    REBYTE *stop = start + (num_pixels * 4);
    while (start < stop) {
        *start++ = 0;  // red
        *start++ = 0;  // green
        *start++ = 0;  // blue
        *start++ = 0xff;  // opaque alpha, R=G=B as 0 means black pixel
    }
}

// Creates WxH image, black pixels, all opaque.
//
inline static REBVAL *Init_Image_Black_Opaque(RELVAL *out, REBCNT w, REBCNT h)
{
    REBSIZ size = (w * h) * 4;  // RGBA pixels, 4 bytes each
    REBBIN *bin = Make_Binary(size);
    SET_SERIES_LEN(bin, size);
    TERM_SERIES(bin);
    Manage_Series(bin);

    RESET_IMAGE(SER_DATA_RAW(bin), (w * h));  // length in 'pixels'

    return Init_Image(out, bin, w, h);
}


// !!! These hooks allow the REB_IMAGE cell type to dispatch to code in the
// IMAGE! extension if it is loaded.
//
extern REBINT CT_Image(const REBCEL *a, const REBCEL *b, REBINT mode);
extern REB_R MAKE_Image(REBVAL *out, enum Reb_Kind kind, const REBVAL *opt_parent, const REBVAL *arg);
extern REB_R TO_Image(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg);
extern void MF_Image(REB_MOLD *mo, const REBCEL *v, bool form);
extern REBTYPE(Image);
extern REB_R PD_Image(REBPVS *pvs, const REBVAL *picker, const REBVAL *opt_setval);
