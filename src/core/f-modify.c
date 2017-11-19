//
//  File: %f-modify.c
//  Summary: "block series modification (insert, append, change)"
//  Section: functional
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


//
//  Modify_Array: C
//
// Returns new dst_idx
//
REBCNT Modify_Array(
    REBCNT action,          // INSERT, APPEND, CHANGE
    REBARR *dst_arr,        // target
    REBCNT dst_idx,         // position
    const REBVAL *src_val,  // source
    REBCNT flags,           // AM_ONLY, AM_PART
    REBINT dst_len,         // length to remove
    REBINT dups             // dup count
) {
    REBCNT tail = ARR_LEN(dst_arr);

    REBINT ilen = 1; // length to be inserted

    const RELVAL *src_rel;
    REBSPC *specifier;

    if (IS_VOID(src_val) || dups < 0) {
        // If they are effectively asking for "no action" then all we have
        // to do is return the natural index result for the operation.
        // (APPEND will return 0, insert the tail of the insertion...so index)

        return (action == SYM_APPEND) ? 0 : dst_idx;
    }

    if (action == SYM_APPEND || dst_idx > tail) dst_idx = tail;

    // Check /PART, compute LEN:
    if (NOT(flags & AM_ONLY) && ANY_ARRAY(src_val)) {
        // Adjust length of insertion if changing /PART:
        if (action != SYM_CHANGE && (flags & AM_PART))
            ilen = dst_len;
        else
            ilen = VAL_LEN_AT(src_val);

        // Are we modifying ourselves? If so, copy src_val block first:
        if (dst_arr == VAL_ARRAY(src_val)) {
            REBARR *copy = Copy_Array_At_Shallow(
                VAL_ARRAY(src_val), VAL_INDEX(src_val), VAL_SPECIFIER(src_val)
            );
            MANAGE_ARRAY(copy); // !!! Review: worth it to not manage and free?
            src_rel = ARR_HEAD(copy);
            specifier = SPECIFIED; // copy already specified it
        }
        else {
            src_rel = VAL_ARRAY_AT(src_val); // skips by VAL_INDEX values
            specifier = VAL_SPECIFIER(src_val);
        }
    }
    else {
        // use passed in RELVAL and specifier
        src_rel = src_val;
        specifier = SPECIFIED; // it's a REBVAL, not a RELVAL, so specified
    }

    REBINT size = dups * ilen; // total to insert

    if (action != SYM_CHANGE) {
        // Always expand dst_arr for INSERT and APPEND actions:
        Expand_Series(SER(dst_arr), dst_idx, size);
    }
    else {
        if (size > dst_len)
            Expand_Series(SER(dst_arr), dst_idx, size-dst_len);
        else if (size < dst_len && (flags & AM_PART))
            Remove_Series(SER(dst_arr), dst_idx, dst_len-size);
        else if (size + dst_idx > tail) {
            EXPAND_SERIES_TAIL(SER(dst_arr), size - (tail - dst_idx));
        }
    }

    tail = (action == SYM_APPEND) ? 0 : size + dst_idx;

#if !defined(NDEBUG)
    if (IS_ARRAY_MANAGED(dst_arr)) {
        REBINT i;
        for (i = 0; i < ilen; ++i)
            ASSERT_VALUE_MANAGED(&src_rel[i]);
    }
#endif

    for (; dups > 0; dups--) {
        REBINT index = 0;
        for (; index < ilen; ++index, ++dst_idx) {
            Derelativize(
                ARR_HEAD(dst_arr) + dst_idx,
                src_rel + index,
                specifier
            );
        }
    }
    TERM_ARRAY_LEN(dst_arr, ARR_LEN(dst_arr));

    ASSERT_ARRAY(dst_arr);

    return tail;
}


//
//  Modify_String: C
//
// Returns new dst_idx.
//
REBCNT Modify_String(
    REBCNT action,          // INSERT, APPEND, CHANGE
    REBSER *dst_ser,        // target
    REBCNT dst_idx,         // position
    const REBVAL *src_val,  // source
    REBFLGS flags,          // AM_PART, AM_BINARY_SERIES
    REBINT dst_len,         // length to remove
    REBINT dups             // dup count
) {
    // For INSERT/PART and APPEND/PART
    //
    REBINT limit;
    if (action != SYM_CHANGE && (flags & AM_PART))
        limit = dst_len; // should be non-negative
    else
        limit = -1;

    if (IS_VOID(src_val) || limit == 0 || dups < 0)
        return action == SYM_APPEND ? 0 : dst_idx;

    REBCNT tail = SER_LEN(dst_ser);
    if (action == SYM_APPEND || dst_idx > tail)
        dst_idx = tail;

    // If the src_val is not a string, then we need to create a string:

    REBCNT src_idx = 0;
    REBCNT src_len;
    REBSER *src_ser;
    REBOOL needs_free;
    if (flags & AM_BINARY_SERIES) {
        if (IS_INTEGER(src_val)) {
            REBI64 i = VAL_INT64(src_val);
            if (i > 255 || i < 0)
                fail ("Inserting out-of-range INTEGER! into BINARY!");

            src_ser = Make_Binary(1);
            *BIN_HEAD(src_ser) = cast(REBYTE, i);
            TERM_BIN_LEN(src_ser, 1);
            needs_free = TRUE;
            limit = -1;
        }
        else if (IS_BLOCK(src_val)) {
            src_ser = Join_Binary(src_val, limit); // NOTE: shared FORM buffer
            needs_free = FALSE;
            limit = -1;
        }
        else if (IS_CHAR(src_val)) {
            //
            // "UTF-8 was originally specified to allow codepoints with up to
            // 31 bits (or 6 bytes). But with RFC3629, this was reduced to 4
            // bytes max. to be more compatible to UTF-16."  So depending on
            // which RFC you consider "the UTF-8", max size is either 4 or 6.
            //
            src_ser = Make_Binary(6);
            SET_SERIES_LEN(
                src_ser,
                Encode_UTF8_Char(BIN_HEAD(src_ser), VAL_CHAR(src_val))
            );
            needs_free = TRUE;
            limit = -1;
        }
        else if (ANY_STRING(src_val)) {
            src_len = VAL_LEN_AT(src_val);
            if (limit >= 0 && src_len > cast(REBCNT, limit))
                src_len = limit;
            src_ser = Make_UTF8_From_Any_String(src_val, src_len, OPT_ENC_0);
            needs_free = TRUE;
            limit = -1;
        }
        else if (IS_BINARY(src_val)) {
            src_ser = NULL;
            needs_free = FALSE;
        }
        else
            fail (src_val);
    }
    else if (IS_CHAR(src_val)) {
        src_ser = Make_Series_Codepoint(VAL_CHAR(src_val));
        needs_free = TRUE;
    }
    else if (IS_BLOCK(src_val)) {
        src_ser = Form_Tight_Block(src_val);
        needs_free = TRUE;
    }
    else if (!ANY_STRING(src_val) || IS_TAG(src_val)) {
        src_ser = Copy_Form_Value(src_val, 0);
        needs_free = TRUE;
    }
    else {
        src_ser = NULL;
        needs_free = FALSE;
    }

    // Use either new src or the one that was passed:
    if (src_ser != NULL) {
        src_len = SER_LEN(src_ser);
    }
    else {
        src_ser = VAL_SERIES(src_val);
        src_idx = VAL_INDEX(src_val);
        src_len = VAL_LEN_AT(src_val);
        assert(needs_free == FALSE);
    }

    if (limit >= 0)
        src_len = limit;

    // If Source == Destination we need to prevent possible conflicts.
    // Clone the argument just to be safe.
    // (Note: It may be possible to optimize special cases like append !!)
    if (dst_ser == src_ser) {
        assert(!needs_free);
        src_ser = Copy_Sequence_At_Len(src_ser, src_idx, src_len);
        needs_free = TRUE;
        src_idx = 0;
    }

    // Total to insert:
    //
    REBINT size = dups * src_len;

    if (action != SYM_CHANGE) {
        // Always expand dst_ser for INSERT and APPEND actions:
        Expand_Series(dst_ser, dst_idx, size);
    } else {
        if (size > dst_len)
            Expand_Series(dst_ser, dst_idx, size - dst_len);
        else if (size < dst_len && (flags & AM_PART))
            Remove_Series(dst_ser, dst_idx, dst_len - size);
        else if (size + dst_idx > tail) {
            EXPAND_SERIES_TAIL(dst_ser, size - (tail - dst_idx));
        }
    }

    // For dup count:
    for (; dups > 0; dups--) {
        Insert_String(dst_ser, dst_idx, src_ser, src_idx, src_len, TRUE);
        dst_idx += src_len;
    }

    TERM_SEQUENCE(dst_ser);

    if (needs_free) {
        // If we did not use the series that was passed in, but rather
        // created an internal temporary one, we need to free it.
        Free_Series(src_ser);
    }

    return (action == SYM_APPEND) ? 0 : dst_idx;
}
