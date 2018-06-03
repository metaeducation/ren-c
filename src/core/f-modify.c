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
    REBSTR *verb,           // INSERT, APPEND, CHANGE
    REBARR *dst_arr,        // target
    REBCNT dst_idx,         // position
    const REBVAL *src_val,  // source
    REBCNT flags,           // AM_SPLICE, AM_PART
    REBINT dst_len,         // length to remove
    REBINT dups             // dup count
){
    REBSYM sym = STR_SYMBOL(verb);
    assert(sym == SYM_INSERT or sym == SYM_CHANGE or sym == SYM_APPEND);

    REBCNT tail = ARR_LEN(dst_arr);

    const RELVAL *src_rel;
    REBSPC *specifier;

    if (IS_NULLED(src_val) or dups <= 0) {
        // If they are effectively asking for "no action" then all we have
        // to do is return the natural index result for the operation.
        // (APPEND will return 0, insert the tail of the insertion...so index)

        return (sym == SYM_APPEND) ? 0 : dst_idx;
    }

    if (sym == SYM_APPEND or dst_idx > tail)
        dst_idx = tail;

    // Each dup being inserted need a newline signal after it if:
    //
    // * The user explicitly invokes the /LINE refinement (AM_LINE flag)
    // * It's a spliced insertion and there's a NEWLINE_BEFORE flag on the
    //   element *after* the last item in the dup
    // * It's a spliced insertion and there dup goes to the end of the array
    //   so there's no element after the last item, but TAIL_NEWLINE is set
    //   on the inserted array.
    //
    REBOOL tail_newline = did (flags & AM_LINE);
    REBINT ilen;

    // Check /PART, compute LEN:
    if (flags & AM_SPLICE) {
        assert(ANY_ARRAY(src_val));
        // Adjust length of insertion if changing /PART:
        if (sym != SYM_CHANGE and (flags & AM_PART))
            ilen = dst_len;
        else
            ilen = VAL_LEN_AT(src_val);

        if (not tail_newline) {
            RELVAL *tail_cell = VAL_ARRAY_AT(src_val) + ilen;
            if (IS_END(tail_cell)) {
                tail_newline = GET_SER_FLAG(
                    VAL_ARRAY(src_val),
                    ARRAY_FLAG_TAIL_NEWLINE
                );
            }
            else if (ilen == 0)
                tail_newline = FALSE;
            else
                tail_newline = GET_VAL_FLAG(
                    tail_cell,
                    VALUE_FLAG_NEWLINE_BEFORE
                );
        }

        // Are we modifying ourselves? If so, copy src_val block first:
        if (dst_arr == VAL_ARRAY(src_val)) {
            REBARR *copy = Copy_Array_At_Extra_Shallow(
                VAL_ARRAY(src_val),
                VAL_INDEX(src_val),
                VAL_SPECIFIER(src_val),
                0, // extra
                NODE_FLAG_MANAGED // !!! Worth it to not manage and free?
            );
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
        ilen = 1;
        src_rel = src_val;
        specifier = SPECIFIED; // it's a REBVAL, not a RELVAL, so specified
    }

    REBINT size = dups * ilen; // total to insert

    // If data is being tacked onto an array, beyond the newlines on the values
    // in that array there is also the chance that there's a newline tail flag
    // on the target, and the insertion is at the end.
    //
    REBOOL head_newline =
        (dst_idx == ARR_LEN(dst_arr))
        and GET_SER_FLAG(dst_arr, ARRAY_FLAG_TAIL_NEWLINE);

    if (sym != SYM_CHANGE) {
        // Always expand dst_arr for INSERT and APPEND actions:
        Expand_Series(SER(dst_arr), dst_idx, size);
    }
    else {
        if (size > dst_len)
            Expand_Series(SER(dst_arr), dst_idx, size - dst_len);
        else if (size < dst_len and (flags & AM_PART))
            Remove_Series(SER(dst_arr), dst_idx, dst_len - size);
        else if (size + dst_idx > tail) {
            EXPAND_SERIES_TAIL(SER(dst_arr), size - (tail - dst_idx));
        }
    }

    tail = (sym == SYM_APPEND) ? 0 : size + dst_idx;

    REBINT dup_index = 0;
    for (; dup_index < dups; ++dup_index) {
        REBINT index = 0;
        for (; index < ilen; ++index, ++dst_idx) {
            Derelativize(
                ARR_HEAD(dst_arr) + dst_idx,
                src_rel + index,
                specifier
            );

            if (dup_index == 0 and index == 0 and head_newline) {
                SET_VAL_FLAG(
                    ARR_HEAD(dst_arr) + dst_idx,
                    VALUE_FLAG_NEWLINE_BEFORE
                );

                // The array flag is not cleared until the loop actually
                // makes a value that will carry on the bit.
                //
                CLEAR_SER_FLAG(dst_arr, ARRAY_FLAG_TAIL_NEWLINE);
                continue;
            }

            if (dup_index > 0 and index == 0 and tail_newline) {
                SET_VAL_FLAG(
                    ARR_HEAD(dst_arr) + dst_idx,
                    VALUE_FLAG_NEWLINE_BEFORE
                );
            }
        }
    }

    // The above loop only puts on (dups - 1) NEWLINE_BEFORE flags.  The
    // last one might have to be the array flag if at tail.
    //
    if (tail_newline) {
        if (dst_idx == ARR_LEN(dst_arr))
            SET_SER_FLAG(dst_arr, ARRAY_FLAG_TAIL_NEWLINE);
        else
            SET_VAL_FLAG(ARR_AT(dst_arr, dst_idx), VALUE_FLAG_NEWLINE_BEFORE);
    }

    if (flags & AM_LINE) {
        //
        // !!! Testing this heuristic: if someone adds a line to an array
        // with the /LINE flag explicitly, force the head element to have a
        // newline.  This allows `x: copy [] | append/line x [a b c]` to give
        // a more common result.  The head line can be removed easily.
        //
        SET_VAL_FLAG(ARR_HEAD(dst_arr), VALUE_FLAG_NEWLINE_BEFORE);
    }

    ASSERT_ARRAY(dst_arr);

    return tail;
}


//
//  Modify_Binary: C
//
// Returns new dst_idx.
//
REBCNT Modify_Binary(
    REBVAL *dst_val,        // target
    REBSTR *verb,            // INSERT, APPEND, CHANGE
    const REBVAL *src_val,  // source
    REBFLGS flags,          // AM_PART
    REBINT dst_len,         // length to remove
    REBINT dups             // dup count
){
    REBSYM sym = STR_SYMBOL(verb);
    assert(sym == SYM_INSERT or sym == SYM_CHANGE or sym == SYM_APPEND);

    REBSER *dst_ser = VAL_SERIES(dst_val);
    REBCNT dst_idx = VAL_INDEX(dst_val);

    // For INSERT/PART and APPEND/PART
    //
    REBINT limit;
    if (sym != SYM_CHANGE && (flags & AM_PART))
        limit = dst_len; // should be non-negative
    else
        limit = -1;

    if (IS_NULLED(src_val) || limit == 0 || dups < 0)
        return sym == SYM_APPEND ? 0 : dst_idx;

    REBCNT tail = SER_LEN(dst_ser);
    if (sym == SYM_APPEND || dst_idx > tail)
        dst_idx = tail;

    // If the src_val is not a string, then we need to create a string:

    REBCNT src_idx = 0;
    REBCNT src_len;
    REBSER *src_ser;
    REBOOL needs_free;
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
        REBCNT len_at = VAL_LEN_AT(src_val);
        if (limit >= 0 && len_at > cast(REBCNT, limit))
            src_ser = Make_UTF8_From_Any_String(src_val, limit);
        else
            src_ser = Make_UTF8_From_Any_String(src_val, len_at);
        needs_free = TRUE;
        limit = -1;
    }
    else if (IS_BINARY(src_val)) {
        src_ser = NULL;
        needs_free = FALSE;
    }
    else
        fail (Error_Invalid(src_val));

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

    if (sym != SYM_CHANGE) {
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
        memcpy(BIN_AT(dst_ser, dst_idx), BIN_AT(src_ser, src_idx), src_len);
        dst_idx += src_len;
    }

    TERM_SEQUENCE(dst_ser);

    if (needs_free) // didn't use original data as-is
        Free_Unmanaged_Series(src_ser);

    return (sym == SYM_APPEND) ? 0 : dst_idx;
}


//
//  Modify_String: C
//
// Returns new dst_idx.
//
REBCNT Modify_String(
    REBVAL *dst_val,        // target
    REBSTR *verb,            // INSERT, APPEND, CHANGE
    const REBVAL *src_val,  // source
    REBFLGS flags,          // AM_PART
    REBINT dst_len,         // length to remove
    REBINT dups             // dup count
){
    REBSYM sym = STR_SYMBOL(verb);
    assert(sym == SYM_INSERT or sym == SYM_CHANGE or sym == SYM_APPEND);

    REBSER *dst_ser = VAL_SERIES(dst_val);
    REBCNT dst_idx = VAL_INDEX(dst_val);

    // For INSERT/PART and APPEND/PART
    //
    REBINT limit;
    if (sym != SYM_CHANGE && (flags & AM_PART))
        limit = dst_len; // should be non-negative
    else
        limit = -1;

    if (IS_NULLED(src_val) || limit == 0 || dups < 0)
        return sym == SYM_APPEND ? 0 : dst_idx;

    REBCNT tail = SER_LEN(dst_ser);
    if (sym == SYM_APPEND or dst_idx > tail)
        dst_idx = tail;

    // If the src_val is not a string, then we need to create a string:

    REBCNT src_idx = 0;
    REBSER *src_ser;
    REBCNT src_len;
    REBOOL needs_free;
    if (IS_CHAR(src_val)) {
        src_ser = Make_Series_Codepoint(VAL_CHAR(src_val));
        src_len = SER_LEN(src_ser);

        needs_free = TRUE;
    }
    else if (IS_BLOCK(src_val)) {
        src_ser = Form_Tight_Block(src_val);
        src_len = SER_LEN(src_ser);

        needs_free = TRUE;
    }
    else if (
        ANY_STRING(src_val)
        and not (IS_TAG(src_val) or (flags & AM_LINE))
    ){
        src_ser = VAL_SERIES(src_val);
        src_idx = VAL_INDEX(src_val);
        src_len = VAL_LEN_AT(src_val);

        needs_free = FALSE;
    }
    else {
        src_ser = Copy_Form_Value(src_val, 0);
        src_len = SER_LEN(src_ser);

        needs_free = TRUE;
    }

    if (limit >= 0)
        src_len = limit;

    // If Source == Destination we need to prevent possible conflicts.
    // Clone the argument just to be safe.
    // (Note: It may be possible to optimize special cases like append !!)
    //
    if (dst_ser == src_ser) {
        assert(!needs_free);
        src_ser = Copy_Sequence_At_Len(src_ser, src_idx, src_len);
        needs_free = TRUE;
        src_idx = 0;
    }

    if (flags & AM_LINE) {
        assert(needs_free); // don't want to modify input series
        Append_Codepoint(src_ser, '\n');
        ++src_len;
    }

    // Total to insert:
    //
    REBINT size = dups * src_len;

    if (sym != SYM_CHANGE) {
        // Always expand dst_ser for INSERT and APPEND actions:
        Expand_Series(dst_ser, dst_idx, size);
    }
    else {
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
        memcpy(
            AS_REBUNI(UNI_AT(dst_ser, dst_idx)),
            AS_REBUNI(UNI_AT(src_ser, src_idx)),
            sizeof(REBUNI) * src_len
        );

        dst_idx += src_len;
    }

    TERM_SEQUENCE(dst_ser);

    if (needs_free) // didn't use original data as-is
        Free_Unmanaged_Series(src_ser);

    return (sym == SYM_APPEND) ? 0 : dst_idx;
}
