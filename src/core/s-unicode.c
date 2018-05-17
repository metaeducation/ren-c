//
// Rebol 3 Language Interpreter and Run-time Environment
// "Ren-C" branch @ https://github.com/metaeducation/ren-c
// REBOL is a trademark of REBOL Technologies
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
//  Project: Rebol 3 Interpreter and Run-time (Ren-C branch)
//  Homepage: https://github.com/metaeducation/ren-c/
//  File: %s-unicode.c
//  Summary: unicode support functions
//  Section: strings
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The top part of this code is from Unicode Inc. The second
// part was added by REBOL Technologies.
//


/*
 * Copyright 2001-2004 Unicode, Inc.
 *
 * Disclaimer
 *
 * This source code is provided as is by Unicode, Inc. No claims are
 * made as to fitness for any particular purpose. No warranties of any
 * kind are expressed or implied. The recipient agrees to determine
 * applicability of information provided. If this file has been
 * purchased on magnetic or optical media from Unicode, Inc., the
 * sole remedy for any claim will be exchange of defective media
 * within 90 days of receipt.
 *
 * Limitations on Rights to Redistribute This Code
 *
 * Unicode, Inc. hereby grants the right to freely use the information
 * supplied in this file in the creation of products supporting the
 * Unicode Standard, and to make copies of this file in any form
 * for internal or external distribution as long as this notice
 * remains attached.
 */

/* ---------------------------------------------------------------------

    Conversions between UTF32, UTF-16, and UTF-8.  Header file.

    Several funtions are included here, forming a complete set of
    conversions between the three formats.  UTF-7 is not included
    here, but is handled in a separate source file.

    Each of these routines takes pointers to input buffers and output
    buffers.  The input buffers are const.

    Each routine converts the text between *sourceStart and sourceEnd,
    putting the result into the buffer between *targetStart and
    targetEnd. Note: the end pointers are *after* the last item: e.g.
    *(sourceEnd - 1) is the last item.

    The return result indicates whether the conversion was successful,
    and if not, whether the problem was in the source or target buffers.
    (Only the first encountered problem is indicated.)

    After the conversion, *sourceStart and *targetStart are both
    updated to point to the end of last text successfully converted in
    the respective buffers.

    Input parameters:
    sourceStart - pointer to a pointer to the source buffer.
        The contents of this are modified on return so that
        it points at the next thing to be converted.
    targetStart - similarly, pointer to pointer to the target buffer.
    sourceEnd, targetEnd - respectively pointers to the ends of the
        two buffers, for overflow checking only.

    These conversion functions take a ConversionFlags argument. When this
    flag is set to strict, both irregular sequences and isolated surrogates
    will cause an error.  When the flag is set to lenient, both irregular
    sequences and isolated surrogates are converted.

    Whether the flag is strict or lenient, all illegal sequences will cause
    an error return. This includes sequences such as: <F4 90 80 80>, <C0 80>,
    or <A0> in UTF-8, and values above 0x10FFFF in UTF-32. Conformant code
    must check for illegal sequences.

    When the flag is set to lenient, characters over 0x10FFFF are converted
    to the replacement character; otherwise (when the flag is set to strict)
    they constitute an error.

    Output parameters:
    The value "sourceIllegal" is returned from some routines if the input
    sequence is malformed.  When "sourceIllegal" is returned, the source
    value will point to the illegal value that caused the problem. E.g.,
    in UTF-8 when a sequence is malformed, it points to the start of the
    malformed sequence.

    Author: Mark E. Davis, 1994.
    Rev History: Rick McGowan, fixes & updates May 2001.
         Fixes & updates, Sept 2001.

------------------------------------------------------------------------ */

#include "sys-core.h"


/* ---------------------------------------------------------------------
    The following 4 definitions are compiler-specific.
    The C standard does not guarantee that wchar_t has at least
    16 bits, so wchar_t is no less portable than unsigned short!
    All should be unsigned values to avoid sign extension during
    bit mask & shift operations.
------------------------------------------------------------------------ */

typedef unsigned long   UTF32;  /* at least 32 bits */
typedef unsigned short  UTF16;  /* at least 16 bits */
typedef unsigned char   UTF8;   /* typically 8 bits */
typedef unsigned char   Boolean; /* 0 or 1 */

/* Some fundamental constants */
#define UNI_REPLACEMENT_CHAR (UTF32)0x0000FFFD
#define UNI_MAX_BMP (UTF32)0x0000FFFF
#define UNI_MAX_UTF16 (UTF32)0x0010FFFF
#define UNI_MAX_UTF32 (UTF32)0x7FFFFFFF
#define UNI_MAX_LEGAL_UTF32 (UTF32)0x0010FFFF

typedef enum {
    conversionOK,       /* conversion successful */
    sourceExhausted,    /* partial character in source, but hit end */
    targetExhausted,    /* insuff. room in target for conversion */
    sourceIllegal       /* source sequence is illegal/malformed */
} ConversionResult;

typedef enum {
    strictConversion = 0,
    lenientConversion
} ConversionFlags;


ConversionResult ConvertUTF8toUTF16 (
        const UTF8** sourceStart, const UTF8* sourceEnd,
        UTF16** targetStart, UTF16* targetEnd, ConversionFlags flags);

ConversionResult ConvertUTF16toUTF8 (
        const UTF16** sourceStart, const UTF16* sourceEnd,
        UTF8** targetStart, UTF8* targetEnd, ConversionFlags flags);

ConversionResult ConvertUTF8toUTF32 (
        const UTF8** sourceStart, const UTF8* sourceEnd,
        UTF32** targetStart, UTF32* targetEnd, ConversionFlags flags);

ConversionResult ConvertUTF32toUTF8 (
        const UTF32** sourceStart, const UTF32* sourceEnd,
        UTF8** targetStart, UTF8* targetEnd, ConversionFlags flags);

ConversionResult ConvertUTF16toUTF32 (
        const UTF16** sourceStart, const UTF16* sourceEnd,
        UTF32** targetStart, UTF32* targetEnd, ConversionFlags flags);

ConversionResult ConvertUTF32toUTF16 (
        const UTF32** sourceStart, const UTF32* sourceEnd,
        UTF16** targetStart, UTF16* targetEnd, ConversionFlags flags);

Boolean isLegalUTF8Sequence(const UTF8 *source, const UTF8 *sourceEnd);

/* ---------------------------------------------------------------------

    Conversions between UTF32, UTF-16, and UTF-8. Source code file.
    Author: Mark E. Davis, 1994.
    Rev History: Rick McGowan, fixes & updates May 2001.
    Sept 2001: fixed const & error conditions per
    mods suggested by S. Parent & A. Lillich.
    June 2002: Tim Dodd added detection and handling of incomplete
    source sequences, enhanced error detection, added casts
    to eliminate compiler warnings.
    July 2003: slight mods to back out aggressive FFFE detection.
    Jan 2004: updated switches in from-UTF8 conversions.
    Oct 2004: updated to use UNI_MAX_LEGAL_UTF32 in UTF-32 conversions.

    See the header file "ConvertUTF.h" for complete documentation.

------------------------------------------------------------------------ */

#ifdef CVTUTF_DEBUG
// #include <stdio.h> // !!! No <stdio.h> in Ren-C release builds
#endif


#define UNI_SUR_HIGH_START  (UTF32)0xD800
#define UNI_SUR_HIGH_END    (UTF32)0xDBFF
#define UNI_SUR_LOW_START   (UTF32)0xDC00
#define UNI_SUR_LOW_END     (UTF32)0xDFFF
#define false      0
#define true        1

/* --------------------------------------------------------------------- */

/*
 * Index into the table below with the first byte of a UTF-8 sequence to
 * get the number of trailing bytes that are supposed to follow it.
 * Note that *legal* UTF-8 values can't have 4 or 5-bytes. The table is
 * left as-is for anyone who may want to do such conversion, which was
 * allowed in earlier algorithms.
 */
static const char trailingBytesForUTF8[256] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 3,3,3,3,3,3,3,3,4,4,4,4,5,5,5,5
};

/*
 * Magic values subtracted from a buffer value during UTF8 conversion.
 * This table contains as many values as there might be trailing bytes
 * in a UTF-8 sequence.
 */
static const UTF32 offsetsFromUTF8[6] = { 0x00000000UL, 0x00003080UL, 0x000E2080UL,
             0x03C82080UL, 0xFA082080UL, 0x82082080UL };

/*
 * Once the bits are split out into bytes of UTF-8, this is a mask OR-ed
 * into the first byte, depending on how many bytes follow.  There are
 * as many entries in this table as there are UTF-8 sequence types.
 * (I.e., one byte sequence, two byte... etc.). Remember that sequencs
 * for *legal* UTF-8 will be 4 or fewer bytes total.
 */
static const UTF8 firstByteMark[7] = { 0x00, 0x00, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC };

/* --------------------------------------------------------------------- */

#ifdef USE_ARCHIVED_UTF8_SOURCE

static const int halfShift  = 10; /* used for shifting by 10 bits */

static const UTF32 halfBase = 0x0010000UL;
static const UTF32 halfMask = 0x3FFUL;

ConversionResult ConvertUTF32toUTF16 (
    const UTF32** sourceStart, const UTF32* sourceEnd,
    UTF16** targetStart, UTF16* targetEnd, ConversionFlags flags) {
    ConversionResult result = conversionOK;
    const UTF32* source = *sourceStart;
    UTF16* target = *targetStart;
    while (source < sourceEnd) {
    UTF32 ch;
    if (target >= targetEnd) {
        result = targetExhausted; break;
    }
    ch = *source++;
    if (ch <= UNI_MAX_BMP) { /* Target is a character <= 0xFFFF */
        /* UTF-16 surrogate values are illegal in UTF-32; 0xffff or 0xfffe are both reserved values */
        if (ch >= UNI_SUR_HIGH_START && ch <= UNI_SUR_LOW_END) {
        if (flags == strictConversion) {
            --source; /* return to the illegal value itself */
            result = sourceIllegal;
            break;
        } else {
            *target++ = UNI_REPLACEMENT_CHAR;
        }
        } else {
        *target++ = (UTF16)ch; /* normal case */
        }
    } else if (ch > UNI_MAX_LEGAL_UTF32) {
        if (flags == strictConversion) {
        result = sourceIllegal;
        } else {
        *target++ = UNI_REPLACEMENT_CHAR;
        }
    } else {
        /* target is a character in range 0xFFFF - 0x10FFFF. */
        if (target + 1 >= targetEnd) {
        --source; /* Back up source pointer! */
        result = targetExhausted; break;
        }
        ch -= halfBase;
        *target++ = (UTF16)((ch >> halfShift) + UNI_SUR_HIGH_START);
        *target++ = (UTF16)((ch & halfMask) + UNI_SUR_LOW_START);
    }
    }
    *sourceStart = source;
    *targetStart = target;
    return result;
}

/* --------------------------------------------------------------------- */

ConversionResult ConvertUTF16toUTF32 (
    const UTF16** sourceStart, const UTF16* sourceEnd,
    UTF32** targetStart, UTF32* targetEnd, ConversionFlags flags) {
    ConversionResult result = conversionOK;
    const UTF16* source = *sourceStart;
    UTF32* target = *targetStart;
    UTF32 ch, ch2;
    while (source < sourceEnd) {
    const UTF16* oldSource = source; /*  In case we have to back up because of target overflow. */
    ch = *source++;
    /* If we have a surrogate pair, convert to UTF32 first. */
    if (ch >= UNI_SUR_HIGH_START && ch <= UNI_SUR_HIGH_END) {
        /* If the 16 bits following the high surrogate are in the source buffer... */
        if (source < sourceEnd) {
        ch2 = *source;
        /* If it's a low surrogate, convert to UTF32. */
        if (ch2 >= UNI_SUR_LOW_START && ch2 <= UNI_SUR_LOW_END) {
            ch = ((ch - UNI_SUR_HIGH_START) << halfShift)
            + (ch2 - UNI_SUR_LOW_START) + halfBase;
            ++source;
        } else if (flags == strictConversion) { /* it's an unpaired high surrogate */
            --source; /* return to the illegal value itself */
            result = sourceIllegal;
            break;
        }
        } else { /* We don't have the 16 bits following the high surrogate. */
        --source; /* return to the high surrogate */
        result = sourceExhausted;
        break;
        }
    } else if (flags == strictConversion) {
        /* UTF-16 surrogate values are illegal in UTF-32 */
        if (ch >= UNI_SUR_LOW_START && ch <= UNI_SUR_LOW_END) {
        --source; /* return to the illegal value itself */
        result = sourceIllegal;
        break;
        }
    }
    if (target >= targetEnd) {
        source = oldSource; /* Back up source pointer! */
        result = targetExhausted; break;
    }
    *target++ = ch;
    }
    *sourceStart = source;
    *targetStart = target;
#ifdef CVTUTF_DEBUG
if (result == sourceIllegal) {
    fprintf(stderr, "ConvertUTF16toUTF32 illegal seq 0x%04x,%04x\n", ch, ch2);
    fflush(stderr);
}
#endif
    return result;
}

/* --------------------------------------------------------------------- */

/* The interface converts a whole buffer to avoid function-call overhead.
 * Constants have been gathered. Loops & conditionals have been removed as
 * much as possible for efficiency, in favor of drop-through switches.
 * (See "Note A" at the bottom of the file for equivalent code.)
 * If your compiler supports it, the "isLegalUTF8" call can be turned
 * into an inline function.
 */

/* --------------------------------------------------------------------- */

ConversionResult ConvertUTF16toUTF8 (
    const UTF16** sourceStart, const UTF16* sourceEnd,
    UTF8** targetStart, UTF8* targetEnd, ConversionFlags flags) {
    ConversionResult result = conversionOK;
    const UTF16* source = *sourceStart;
    UTF8* target = *targetStart;
    while (source < sourceEnd) {
    UTF32 ch;
    unsigned short bytesToWrite = 0;
    const UTF32 byteMask = 0xBF;
    const UTF32 byteMark = 0x80;
    const UTF16* oldSource = source; /* In case we have to back up because of target overflow. */
    ch = *source++;
    /* If we have a surrogate pair, convert to UTF32 first. */
    if (ch >= UNI_SUR_HIGH_START && ch <= UNI_SUR_HIGH_END) {
        /* If the 16 bits following the high surrogate are in the source buffer... */
        if (source < sourceEnd) {
        UTF32 ch2 = *source;
        /* If it's a low surrogate, convert to UTF32. */
        if (ch2 >= UNI_SUR_LOW_START && ch2 <= UNI_SUR_LOW_END) {
            ch = ((ch - UNI_SUR_HIGH_START) << halfShift)
            + (ch2 - UNI_SUR_LOW_START) + halfBase;
            ++source;
        } else if (flags == strictConversion) { /* it's an unpaired high surrogate */
            --source; /* return to the illegal value itself */
            result = sourceIllegal;
            break;
        }
        } else { /* We don't have the 16 bits following the high surrogate. */
        --source; /* return to the high surrogate */
        result = sourceExhausted;
        break;
        }
    } else if (flags == strictConversion) {
        /* UTF-16 surrogate values are illegal in UTF-32 */
        if (ch >= UNI_SUR_LOW_START && ch <= UNI_SUR_LOW_END) {
        --source; /* return to the illegal value itself */
        result = sourceIllegal;
        break;
        }
    }
    /* Figure out how many bytes the result will require */
    if (ch < (UTF32)0x80) {      bytesToWrite = 1;
    } else if (ch < (UTF32)0x800) {     bytesToWrite = 2;
    } else if (ch < (UTF32)0x10000) {   bytesToWrite = 3;
    } else if (ch < (UTF32)0x110000) {  bytesToWrite = 4;
    } else {                bytesToWrite = 3;
                        ch = UNI_REPLACEMENT_CHAR;
    }

    target += bytesToWrite;
    if (target > targetEnd) {
        source = oldSource; /* Back up source pointer! */
        target -= bytesToWrite; result = targetExhausted; break;
    }
    switch (bytesToWrite) { /* note: everything falls through. */
        case 4: *--target = (UTF8)((ch | byteMark) & byteMask); ch >>= 6;
        case 3: *--target = (UTF8)((ch | byteMark) & byteMask); ch >>= 6;
        case 2: *--target = (UTF8)((ch | byteMark) & byteMask); ch >>= 6;
        case 1: *--target =  (UTF8)(ch | firstByteMark[bytesToWrite]);
    }
    target += bytesToWrite;
    }
    *sourceStart = source;
    *targetStart = target;
    return result;
}
#endif // USE_ARCHIVED_UTF8_SOURCE

/* --------------------------------------------------------------------- */

/*
 * Utility routine to tell whether a sequence of bytes is legal UTF-8.
 * This must be called with the length pre-determined by the first byte.
 * If not calling this from ConvertUTF8to*, then the length can be set by:
 *  length = trailingBytesForUTF8[*source]+1;
 * and the sequence is illegal right away if there aren't that many bytes
 * available.
 * If presented with a length > 4, this returns false.  The Unicode
 * definition of UTF-8 goes up to 4-byte sequences.
 */

static Boolean isLegalUTF8(const UTF8 *source, int length) {
    UTF8 a;
    const UTF8 *srcptr = source+length;

    switch (length) {
    default: return false;
    /* Everything else falls through when "true"... */
    case 4: if ((a = (*--srcptr)) < 0x80 || a > 0xBF) return false; // falls through
    case 3: if ((a = (*--srcptr)) < 0x80 || a > 0xBF) return false; // falls through
    case 2: if ((a = (*--srcptr)) > 0xBF) return false; // falls through

        switch (*source) {
            /* no fall-through in this inner switch */
            case 0xE0: if (a < 0xA0) return false; break;
            case 0xED: if (a > 0x9F) return false; break;
            case 0xF0: if (a < 0x90) return false; break;
            case 0xF4: if (a > 0x8F) return false; break;
            default:   if (a < 0x80) return false; break;
        }

        // falls through
    case 1: if (*source >= 0x80 && *source < 0xC2) return false;
    }

    if (*source > 0xF4) return false;

    return true;
}

/* --------------------------------------------------------------------- */

/*
 * Exported function to return whether a UTF-8 sequence is legal or not.
 * This is not used here; it's just exported.
 */
Boolean isLegalUTF8Sequence(const UTF8 *source, const UTF8 *sourceEnd) {
    int length = trailingBytesForUTF8[*source]+1;
    if (source+length > sourceEnd) return false;
    return isLegalUTF8(source, length);
}

/* --------------------------------------------------------------------- */
#ifdef USE_ARCHIVED_UTF16_CODE
ConversionResult ConvertUTF8toUTF16 (
    const UTF8** sourceStart, const UTF8* sourceEnd,
    UTF16** targetStart, UTF16* targetEnd, ConversionFlags flags) {
    ConversionResult result = conversionOK;
    const UTF8* source = *sourceStart;
    UTF16* target = *targetStart;
    while (source < sourceEnd) {
    UTF32 ch = 0;
    unsigned short extraBytesToRead = trailingBytesForUTF8[*source];
    if (source + extraBytesToRead >= sourceEnd) {
        result = sourceExhausted; break;
    }
    /* Do this check whether lenient or strict */
    if (! isLegalUTF8(source, extraBytesToRead+1)) {
        result = sourceIllegal;
        break;
    }
    /*
     * The cases all fall through. See "Note A" below.
     */
    switch (extraBytesToRead) {
        case 5: ch += *source++; ch <<= 6; /* remember, illegal UTF-8 */
        case 4: ch += *source++; ch <<= 6; /* remember, illegal UTF-8 */
        case 3: ch += *source++; ch <<= 6;
        case 2: ch += *source++; ch <<= 6;
        case 1: ch += *source++; ch <<= 6;
        case 0: ch += *source++;
    }
    ch -= offsetsFromUTF8[extraBytesToRead];

    if (target >= targetEnd) {
        source -= (extraBytesToRead+1); /* Back up source pointer! */
        result = targetExhausted; break;
    }
    if (ch <= UNI_MAX_BMP) { /* Target is a character <= 0xFFFF */
        /* UTF-16 surrogate values are illegal in UTF-32 */
        if (ch >= UNI_SUR_HIGH_START && ch <= UNI_SUR_LOW_END) {
        if (flags == strictConversion) {
            source -= (extraBytesToRead+1); /* return to the illegal value itself */
            result = sourceIllegal;
            break;
        } else {
            *target++ = UNI_REPLACEMENT_CHAR;
        }
        } else {
        *target++ = (UTF16)ch; /* normal case */
        }
    } else if (ch > UNI_MAX_UTF16) {
        if (flags == strictConversion) {
        result = sourceIllegal;
        source -= (extraBytesToRead+1); /* return to the start */
        break; /* Bail out; shouldn't continue */
        } else {
        *target++ = UNI_REPLACEMENT_CHAR;
        }
    } else {
        /* target is a character in range 0xFFFF - 0x10FFFF. */
        if (target + 1 >= targetEnd) {
        source -= (extraBytesToRead+1); /* Back up source pointer! */
        result = targetExhausted; break;
        }
        ch -= halfBase;
        *target++ = (UTF16)((ch >> halfShift) + UNI_SUR_HIGH_START);
        *target++ = (UTF16)((ch & halfMask) + UNI_SUR_LOW_START);
    }
    }
    *sourceStart = source;
    *targetStart = target;
    return result;
}

/* --------------------------------------------------------------------- */

ConversionResult ConvertUTF32toUTF8 (
    const UTF32** sourceStart, const UTF32* sourceEnd,
    UTF8** targetStart, UTF8* targetEnd, ConversionFlags flags) {
    ConversionResult result = conversionOK;
    const UTF32* source = *sourceStart;
    UTF8* target = *targetStart;
    while (source < sourceEnd) {
    UTF32 ch;
    unsigned short bytesToWrite = 0;
    const UTF32 byteMask = 0xBF;
    const UTF32 byteMark = 0x80;
    ch = *source++;
    if (flags == strictConversion ) {
        /* UTF-16 surrogate values are illegal in UTF-32 */
        if (ch >= UNI_SUR_HIGH_START && ch <= UNI_SUR_LOW_END) {
        --source; /* return to the illegal value itself */
        result = sourceIllegal;
        break;
        }
    }
    /*
     * Figure out how many bytes the result will require. Turn any
     * illegally large UTF32 things (> Plane 17) into replacement chars.
     */
    if (ch < (UTF32)0x80) {      bytesToWrite = 1;
    } else if (ch < (UTF32)0x800) {     bytesToWrite = 2;
    } else if (ch < (UTF32)0x10000) {   bytesToWrite = 3;
    } else if (ch <= UNI_MAX_LEGAL_UTF32) {  bytesToWrite = 4;
    } else {                bytesToWrite = 3;
                        ch = UNI_REPLACEMENT_CHAR;
                        result = sourceIllegal;
    }

    target += bytesToWrite;
    if (target > targetEnd) {
        --source; /* Back up source pointer! */
        target -= bytesToWrite; result = targetExhausted; break;
    }
    switch (bytesToWrite) { /* note: everything falls through. */
        case 4: *--target = (UTF8)((ch | byteMark) & byteMask); ch >>= 6;
        case 3: *--target = (UTF8)((ch | byteMark) & byteMask); ch >>= 6;
        case 2: *--target = (UTF8)((ch | byteMark) & byteMask); ch >>= 6;
        case 1: *--target = (UTF8) (ch | firstByteMark[bytesToWrite]);
    }
    target += bytesToWrite;
    }
    *sourceStart = source;
    *targetStart = target;
    return result;
}

/* --------------------------------------------------------------------- */

ConversionResult ConvertUTF8toUTF32 (
    const UTF8** sourceStart, const UTF8* sourceEnd,
    UTF32** targetStart, UTF32* targetEnd, ConversionFlags flags) {
    ConversionResult result = conversionOK;
    const UTF8* source = *sourceStart;
    UTF32* target = *targetStart;
    while (source < sourceEnd) {
    UTF32 ch = 0;
    unsigned short extraBytesToRead = trailingBytesForUTF8[*source];
    if (source + extraBytesToRead >= sourceEnd) {
        result = sourceExhausted; break;
    }
    /* Do this check whether lenient or strict */
    if (! isLegalUTF8(source, extraBytesToRead+1)) {
        result = sourceIllegal;
        break;
    }
    /*
     * The cases all fall through. See "Note A" below.
     */
    switch (extraBytesToRead) {
        case 5: ch += *source++; ch <<= 6;
        case 4: ch += *source++; ch <<= 6;
        case 3: ch += *source++; ch <<= 6;
        case 2: ch += *source++; ch <<= 6;
        case 1: ch += *source++; ch <<= 6;
        case 0: ch += *source++;
    }
    ch -= offsetsFromUTF8[extraBytesToRead];

    if (target >= targetEnd) {
        source -= (extraBytesToRead+1); /* Back up the source pointer! */
        result = targetExhausted; break;
    }
    if (ch <= UNI_MAX_LEGAL_UTF32) {
        /*
         * UTF-16 surrogate values are illegal in UTF-32, and anything
         * over Plane 17 (> 0x10FFFF) is illegal.
         */
        if (ch >= UNI_SUR_HIGH_START && ch <= UNI_SUR_LOW_END) {
        if (flags == strictConversion) {
            source -= (extraBytesToRead+1); /* return to the illegal value itself */
            result = sourceIllegal;
            break;
        } else {
            *target++ = UNI_REPLACEMENT_CHAR;
        }
        } else {
        *target++ = ch;
        }
    } else { /* i.e., ch > UNI_MAX_LEGAL_UTF32 */
        result = sourceIllegal;
        *target++ = UNI_REPLACEMENT_CHAR;
    }
    }
    *sourceStart = source;
    *targetStart = target;
    return result;
}

/* ---------------------------------------------------------------------

    Note A.
    The fall-through switches in UTF-8 reading code save a
    temp variable, some decrements & conditionals.  The switches
    are equivalent to the following loop:
    {
        int tmpBytesToRead = extraBytesToRead+1;
        do {
        ch += *source++;
        --tmpBytesToRead;
        if (tmpBytesToRead) ch <<= 6;
        } while (tmpBytesToRead > 0);
    }
    In UTF-8 writing code, the switches on "bytesToWrite" are
    similarly unrolled loops.

   --------------------------------------------------------------------- */

#endif //unused


/***********************************************************************
************************************************************************
**
**  Code below added by REBOL Technologies 2008
**
************************************************************************
***********************************************************************/


//
//  Legal_UTF8_Char: C
//
// Returns TRUE if char is legal.
//
// !!! Not currently used.
//
REBOOL Legal_UTF8_Char(const REBYTE *str, REBCNT len)
{
    return did isLegalUTF8Sequence(str, str + len); // different Boolean
}


//
//  Check_UTF8: C
//
// Returns NULL for success, else pointer in the data where error occurred.
//
// Currently not used in the system (all UTF-8 checking is done on the fly)
// but provided as a native via INVALID-UTF8?
//
REBYTE *Check_UTF8(REBYTE *utf8, size_t size)
{
    REBYTE *end = utf8 + size;

    REBCNT trail;
    for (; utf8 != end; utf8 += trail) {
        trail = trailingBytesForUTF8[*utf8] + 1;
        if (utf8 + trail > end || !isLegalUTF8(utf8, trail))
            return utf8;
    }

    return NULL;
}


//
//  Back_Scan_UTF8_Char_Core: C
//
// Converts a single UTF8 code-point and returns the position *at the
// the last byte of the character's data*.  (This differs from the usual
// `Scan_XXX` interface of returning the position after the scanned
// element, ready to read the next one.)
//
// The peculiar interface is useful in loops that are processing
// ordinary ASCII chars directly -as well- as UTF8 ones.  The loop can
// do a single byte pointer increment after both kinds of
// elements, avoiding the need to call any kind of `Scan_Ascii()`:
//
//     for (; len > 0; bp++, len--) {
//         if (*bp < 0x80) {
//             // do ASCII stuff...
//         }
//         else {
//             REBUNI uni;
//             bp = Back_Scan_UTF8_Char(&uni, bp, &len);
//             // do UNICODE stuff...
//         }
//     }
//
// The third parameter is an optional length that will be decremented by
// the number of "extra" bytes the UTF8 has beyond a single byte character.
// This allows for decrement-style loops such as the above.
//
// Prescans source for null, and will not return code point 0.
//
// If failure due to insufficient data or malformed bytes, then NULL is
// returned (len is not advanced).
//
const REBYTE *Back_Scan_UTF8_Char_Core(
    unsigned long *out, // "UTF32" is defined as unsigned long above
    const REBYTE *bp,
    REBCNT *len
) {
    *out = 0;

    const UTF8 *source = bp;
    REBCNT trail = trailingBytesForUTF8[*source];

    // Check that we have enough valid source bytes:
    if (len) {
        if (trail + 1 > *len)
            return NULL;
    }
    else if (trail != 0) {
        do {
            if (source[trail] < 0x80)
                return NULL;
        } while (--trail != 0);

        trail = trailingBytesForUTF8[*source];
    }

    // Do this check whether lenient or strict:
    // if (!isLegalUTF8(source, slen+1)) return 0;

    switch (trail) {
        case 5: *out += *source++; *out <<= 6; // falls through
        case 4: *out += *source++; *out <<= 6; // falls through
        case 3: *out += *source++; *out <<= 6; // falls through
        case 2: *out += *source++; *out <<= 6; // falls through
        case 1: *out += *source++; *out <<= 6; // falls through
        case 0: *out += *source++;
    }
    *out -= offsetsFromUTF8[trail];

    // UTF-16 surrogate values are illegal in UTF-32, and anything
    // over Plane 17 (> 0x10FFFF) is illegal.
    //
    // !!! Is this still relevant, in a system that is fully UTF8 based?
    //
    if (*out > UNI_MAX_LEGAL_UTF32)
        return NULL;
    if (*out >= UNI_SUR_HIGH_START && *out <= UNI_SUR_LOW_END)
        return NULL;

    if (len)
        *len -= trail;

    // !!! Original implementation used 0 as a return value to indicate a
    // decoding failure.  However, 0 is a legal UTF8 codepoint, and also
    // Rebol strings are able to store NUL characters (they track a length
    // and are not zero-terminated.)  Should this be legal?
    //
    assert(*out != 0);
    if (*out == 0)
        return NULL;

    return bp + trail;
}


//
//  Size_As_UTF8: C
//
// Returns how long the UTF8 encoded string would be.
//
// !!! There's a hardcoded table of byte lengths which is used other places,
// it would probably speed this up.
//
size_t Size_As_UTF8(const REBUNI *up, REBCNT len)
{
    size_t size = 0;

    for (; len > 0; len--) {
        UTF32 c = *up++;
        if (c < cast(UTF32, 0x80))
            size++;
        else if (c < cast(UTF32, 0x800))
            size += 2;
        else if (c < cast(UTF32, 0x10000))
            size += 3;
        else if (c <= UNI_MAX_LEGAL_UTF32)
            size += 4;
        else
            size += 3;
    }

    return size;
}


//
//  Encode_UTF8_Char: C
//
// Converts a single char to UTF8 code-point.
// Returns length of char stored in dst.
// Be sure dst has at least 4 bytes available.
//
REBCNT Encode_UTF8_Char(REBYTE *dst, uint32_t c)
{
    int len = 0;
    const uint32_t mask = 0xBF;
    const uint32_t mark = 0x80;

    if (c < cast(uint32_t, 0x80))
        len = 1;
    else if (c < cast(uint32_t, 0x800))
        len = 2;
    else if (c < cast(uint32_t, 0x10000))
        len = 3;
    else if (c <= UNI_MAX_LEGAL_UTF32)
        len = 4;
    else { // !!! Should this fail() instead of pick a replacement char?
        len = 3;
        c = UNI_REPLACEMENT_CHAR;
    }

    dst += len;

    switch (len) {
    case 4:
        *--dst = cast(REBYTE, (c | mark) & mask);
        c >>= 6; // falls through
    case 3:
        *--dst = cast(REBYTE, (c | mark) & mask);
        c >>= 6; // falls through
    case 2:
        *--dst = cast(REBYTE, (c | mark) & mask);
        c >>= 6; // falls through
    case 1:
        *--dst = cast(REBYTE, c | firstByteMark[len]);
    }

    return len;
}


//
//  Encode_UTF8: C
//
// Encode the unicode into UTF8 byte string.
//
// Max is the maximum size of the result (UTF8).
// Returns number of dst bytes used.
// Updates len for source chars used.
// Does not add a terminator.
//
REBCNT Encode_UTF8(
    REBYTE *dst,
    REBCNT max,
    const REBUNI *src,
    REBCNT *len
){
    REBYTE buf[8];

    REBCNT cnt = *len;

    REBYTE *bs = dst; // save start
    const REBUNI *up = src;

    for (; max > 0 && cnt > 0; cnt--) {
        REBUNI c = *up++;
        if (c < 0x80) {
            *dst++ = cast(REBYTE, c);
            max--;
        }
        else {
            REBINT n = Encode_UTF8_Char(buf, c);
            if (n > cast(REBINT, max)) {
                up--;
                break;
            }
            memcpy(dst, buf, n);
            dst += n;
            max -= n;
        }
    }

    *len = up - src;

    return dst - bs;
}


//
//  Make_UTF8_From_Any_String: C
//
// !!! With UTF-8 Everywhere, strings will already be in UTF-8.
//
REBSER *Make_UTF8_From_Any_String(const RELVAL *any_string, REBCNT len) {
    assert(ANY_STRING(any_string));

    const REBUNI *data = VAL_UNI_AT(any_string);
    size_t size = Size_As_UTF8(data, len);
    REBSER *bin = Make_Binary(size);
    SET_SERIES_LEN(bin, Encode_UTF8(BIN_HEAD(bin), size, data, &len));
    assert(SER_LEN(bin) == size);
    TERM_SEQUENCE(bin);
    return bin;
}
