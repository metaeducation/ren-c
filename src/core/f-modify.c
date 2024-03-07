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
REBLEN Modify_Array(
    SymId op,           // INSERT, APPEND, CHANGE
    Array* dst_arr,        // target
    REBLEN dst_idx,         // position
    const Value* src_val,  // source
    REBLEN flags,           // AM_SPLICE, AM_PART
    REBINT dst_len,         // length to remove
    REBINT dups             // dup count
){
    assert(not IS_NULLED(src_val));

    assert(op == SYM_INSERT or op == SYM_CHANGE or op == SYM_APPEND);

    REBLEN tail = Array_Len(dst_arr);

    const Cell* src_rel;
    Specifier* specifier;

    if (IS_VOID(src_val) and op == SYM_CHANGE) {
        //
        // Tweak requests to CHANGE to a null to be a deletion; basically
        // what happens with an empty block.
        //
        flags |= AM_SPLICE;
        src_val = EMPTY_BLOCK;
    }

    if (IS_VOID(src_val) or dups <= 0) {
        // If they are effectively asking for "no action" then all we have
        // to do is return the natural index result for the operation.
        // (APPEND will return 0, insert the tail of the insertion...so index)

        return (op == SYM_APPEND) ? 0 : dst_idx;
    }

    if (op == SYM_APPEND or dst_idx > tail)
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
    bool tail_newline = did (flags & AM_LINE);
    REBINT ilen;

    // Check /PART, compute LEN:
    if (flags & AM_SPLICE) {
        assert(ANY_ARRAY(src_val));
        // Adjust length of insertion if changing /PART:
        if (op != SYM_CHANGE and (flags & AM_PART))
            ilen = dst_len;
        else
            ilen = VAL_LEN_AT(src_val);

        if (not tail_newline) {
            Cell* tail_cell = Cell_Array_At(src_val) + ilen;
            if (IS_END(tail_cell)) {
                tail_newline = GET_SER_FLAG(
                    Cell_Array(src_val),
                    ARRAY_FLAG_TAIL_NEWLINE
                );
            }
            else if (ilen == 0)
                tail_newline = false;
            else
                tail_newline = GET_VAL_FLAG(
                    tail_cell,
                    VALUE_FLAG_NEWLINE_BEFORE
                );
        }

        // Are we modifying ourselves? If so, copy src_val block first:
        if (dst_arr == Cell_Array(src_val)) {
            Array* copy = Copy_Array_At_Extra_Shallow(
                Cell_Array(src_val),
                VAL_INDEX(src_val),
                VAL_SPECIFIER(src_val),
                0, // extra
                NODE_FLAG_MANAGED // !!! Worth it to not manage and free?
            );
            src_rel = Array_Head(copy);
            specifier = SPECIFIED; // copy already specified it
        }
        else {
            src_rel = Cell_Array_At(src_val); // skips by VAL_INDEX values
            specifier = VAL_SPECIFIER(src_val);
        }
    }
    else {
        // use passed in Cell and specifier
        ilen = 1;
        src_rel = src_val;
        specifier = SPECIFIED; // it's a Value, not relative Cell, so specified
    }

    REBINT size = dups * ilen; // total to insert

    // If data is being tacked onto an array, beyond the newlines on the values
    // in that array there is also the chance that there's a newline tail flag
    // on the target, and the insertion is at the end.
    //
    bool head_newline =
        (dst_idx == Array_Len(dst_arr))
        and GET_SER_FLAG(dst_arr, ARRAY_FLAG_TAIL_NEWLINE);

    if (op != SYM_CHANGE) {
        // Always expand dst_arr for INSERT and APPEND actions:
        Expand_Series(dst_arr, dst_idx, size);
    }
    else {
        if (size > dst_len)
            Expand_Series(dst_arr, dst_idx, size - dst_len);
        else if (size < dst_len and (flags & AM_PART))
            Remove_Series(dst_arr, dst_idx, dst_len - size);
        else if (size + dst_idx > tail) {
            Expand_Series_Tail(dst_arr, size - (tail - dst_idx));
        }
    }

    tail = (op == SYM_APPEND) ? 0 : size + dst_idx;

    REBINT dup_index = 0;
    for (; dup_index < dups; ++dup_index) {
        REBINT index = 0;
        for (; index < ilen; ++index, ++dst_idx) {
            Derelativize(
                Array_Head(dst_arr) + dst_idx,
                src_rel + index,
                specifier
            );

            if (dup_index == 0 and index == 0 and head_newline) {
                SET_VAL_FLAG(
                    Array_Head(dst_arr) + dst_idx,
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
                    Array_Head(dst_arr) + dst_idx,
                    VALUE_FLAG_NEWLINE_BEFORE
                );
            }
        }
    }

    // The above loop only puts on (dups - 1) NEWLINE_BEFORE flags.  The
    // last one might have to be the array flag if at tail.
    //
    if (tail_newline) {
        if (dst_idx == Array_Len(dst_arr))
            SET_SER_FLAG(dst_arr, ARRAY_FLAG_TAIL_NEWLINE);
        else
            SET_VAL_FLAG(Array_At(dst_arr, dst_idx), VALUE_FLAG_NEWLINE_BEFORE);
    }

    if (flags & AM_LINE) {
        //
        // !!! Testing this heuristic: if someone adds a line to an array
        // with the /LINE flag explicitly, force the head element to have a
        // newline.  This allows `x: copy [] | append/line x [a b c]` to give
        // a more common result.  The head line can be removed easily.
        //
        SET_VAL_FLAG(Array_Head(dst_arr), VALUE_FLAG_NEWLINE_BEFORE);
    }

    Assert_Array(dst_arr);

    return tail;
}


//
//  Modify_Binary: C
//
// Returns new dst_idx.
//
REBLEN Modify_Binary(
    Value* dst_val,        // target
    SymId op,            // INSERT, APPEND, CHANGE
    const Value* src_val,  // source
    REBFLGS flags,          // AM_PART
    REBINT dst_len,         // length to remove
    REBINT dups             // dup count
){
    assert(not IS_NULLED(src_val));

    assert(op == SYM_INSERT or op == SYM_CHANGE or op == SYM_APPEND);

    Series* dst_ser = VAL_SERIES(dst_val);
    REBLEN dst_idx = VAL_INDEX(dst_val);

    // For INSERT/PART and APPEND/PART
    //
    REBINT limit;
    if (op != SYM_CHANGE && (flags & AM_PART))
        limit = dst_len; // should be non-negative
    else
        limit = -1;

    if (IS_VOID(src_val) and op == SYM_CHANGE) {
        //
        // Tweak requests to CHANGE to a null to be a deletion; basically
        // what happens with an empty binary.
        //
        flags |= AM_SPLICE;
        src_val = EMPTY_BINARY;
    }

    if (IS_VOID(src_val) || limit == 0 || dups < 0)
        return op == SYM_APPEND ? 0 : dst_idx;

    REBLEN tail = Series_Len(dst_ser);
    if (op == SYM_APPEND || dst_idx > tail)
        dst_idx = tail;

    // If the src_val is not a string, then we need to create a string:

    REBLEN src_idx = 0;
    REBLEN src_len;
    Binary* src_ser;
    bool needs_free;
    if (IS_INTEGER(src_val)) {
        REBI64 i = VAL_INT64(src_val);
        if (i > 255 || i < 0)
            fail ("Inserting out-of-range INTEGER! into BINARY!");

        src_ser = Make_Binary(1);
        *Binary_Head(src_ser) = cast(Byte, i);
        Term_Binary_Len(src_ser, 1);
        needs_free = true;
        limit = -1;
    }
    else if (IS_BLOCK(src_val)) {
        src_ser = Join_Binary(src_val, limit); // NOTE: shared FORM buffer
        needs_free = false;
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
        Set_Series_Len(
            src_ser,
            Encode_UTF8_Char(Binary_Head(src_ser), VAL_CHAR(src_val))
        );
        needs_free = true;
        limit = -1;
    }
    else if (ANY_STRING(src_val)) {
        REBLEN len_at = VAL_LEN_AT(src_val);
        if (limit >= 0 && len_at > cast(REBLEN, limit))
            src_ser = Make_Utf8_From_Cell_String_At_Limit(src_val, limit);
        else
            src_ser = Make_Utf8_From_Cell_String_At_Limit(src_val, len_at);
        needs_free = true;
        limit = -1;
    }
    else if (IS_BINARY(src_val)) {
        src_ser = nullptr;
        needs_free = false;
    }
    else
        fail (Error_Invalid(src_val));

    // Use either new src or the one that was passed:
    if (src_ser != nullptr) {
        src_len = Series_Len(src_ser);
    }
    else {
        src_ser = Cell_Binary(src_val);
        src_idx = VAL_INDEX(src_val);
        src_len = VAL_LEN_AT(src_val);
        assert(needs_free == false);
    }

    if (limit >= 0)
        src_len = limit;

    // If Source == Destination we need to prevent possible conflicts.
    // Clone the argument just to be safe.
    // (Note: It may be possible to optimize special cases like append !!)
    if (dst_ser == src_ser) {
        assert(!needs_free);
        src_ser = cast(Binary*,
            Copy_Sequence_At_Len(src_ser, src_idx, src_len)
        );
        needs_free = true;
        src_idx = 0;
    }

    // Total to insert:
    //
    REBINT size = dups * src_len;

    if (op != SYM_CHANGE) {
        // Always expand dst_ser for INSERT and APPEND actions:
        Expand_Series(dst_ser, dst_idx, size);
    } else {
        if (size > dst_len)
            Expand_Series(dst_ser, dst_idx, size - dst_len);
        else if (size < dst_len && (flags & AM_PART))
            Remove_Series(dst_ser, dst_idx, dst_len - size);
        else if (size + dst_idx > tail) {
            Expand_Series_Tail(dst_ser, size - (tail - dst_idx));
        }
    }

    // For dup count:
    for (; dups > 0; dups--) {
        memcpy(
            Binary_At(dst_ser, dst_idx),
            Binary_At(src_ser, src_idx),
            src_len
        );
        dst_idx += src_len;
    }

    TERM_SEQUENCE(dst_ser);

    if (needs_free) // didn't use original data as-is
        Free_Unmanaged_Series(src_ser);

    return (op == SYM_APPEND) ? 0 : dst_idx;
}


//
//  Modify_String: C
//
// Returns new dst_idx.
//
REBLEN Modify_String(
    Value* dst_val,        // target
    SymId op,            // INSERT, APPEND, CHANGE
    const Value* src_val,  // source
    REBFLGS flags,          // AM_PART
    REBINT dst_len,         // length to remove
    REBINT dups             // dup count
){
    assert(not IS_NULLED(src_val));

    assert(op == SYM_INSERT or op == SYM_CHANGE or op == SYM_APPEND);

    String* dst_ser = Cell_String(dst_val);
    REBLEN dst_idx = VAL_INDEX(dst_val);

    // For INSERT/PART and APPEND/PART
    //
    REBINT limit;
    if (op != SYM_CHANGE && (flags & AM_PART))
        limit = dst_len; // should be non-negative
    else
        limit = -1;

    if (IS_VOID(src_val) and op == SYM_CHANGE) {
        //
        // Tweak requests to CHANGE to a null to be a deletion; basically
        // what happens with an empty string.
        //
        flags |= AM_SPLICE;
        src_val = EMPTY_TEXT;
    }

    if (IS_VOID(src_val) || limit == 0 || dups < 0)
        return op == SYM_APPEND ? 0 : dst_idx;

    REBLEN tail = Series_Len(dst_ser);
    if (op == SYM_APPEND or dst_idx > tail)
        dst_idx = tail;

    // If the src_val is not a string, then we need to create a string:

    REBLEN src_idx = 0;
    String* src_ser;
    REBLEN src_len;
    bool needs_free;
    if (IS_CHAR(src_val)) {
        src_ser = Make_Ser_Codepoint(VAL_CHAR(src_val));
        src_len = Series_Len(src_ser);

        needs_free = true;
    }
    else if (IS_BLOCK(src_val)) {
        src_ser = Form_Tight_Block(src_val);
        src_len = Series_Len(src_ser);

        needs_free = true;
    }
    else if (
        ANY_STRING(src_val)
        and not (IS_TAG(src_val) or (flags & AM_LINE))
    ){
        src_ser = Cell_String(src_val);
        src_idx = VAL_INDEX(src_val);
        src_len = VAL_LEN_AT(src_val);

        needs_free = false;
    }
    else {
        src_ser = Copy_Form_Value(src_val, 0);
        src_len = Series_Len(src_ser);

        needs_free = true;
    }

    if (limit >= 0)
        src_len = limit;

    // If Source == Destination we need to prevent possible conflicts.
    // Clone the argument just to be safe.
    // (Note: It may be possible to optimize special cases like append !!)
    //
    if (dst_ser == src_ser) {
        assert(!needs_free);
        src_ser = cast(String*,
            Copy_Sequence_At_Len(src_ser, src_idx, src_len)
        );
        needs_free = true;
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

    if (op != SYM_CHANGE) {
        // Always expand dst_ser for INSERT and APPEND actions:
        Expand_Series(dst_ser, dst_idx, size);
    }
    else {
        if (size > dst_len)
            Expand_Series(dst_ser, dst_idx, size - dst_len);
        else if (size < dst_len && (flags & AM_PART))
            Remove_Series(dst_ser, dst_idx, dst_len - size);
        else if (size + dst_idx > tail) {
            Expand_Series_Tail(dst_ser, size - (tail - dst_idx));
        }
    }

    // For dup count:
    for (; dups > 0; dups--) {
        memcpy(
            AS_REBUNI(String_At(dst_ser, dst_idx)),
            AS_REBUNI(String_At(src_ser, src_idx)),
            sizeof(REBUNI) * src_len
        );

        dst_idx += src_len;
    }

    TERM_SEQUENCE(dst_ser);

    if (needs_free) // didn't use original data as-is
        Free_Unmanaged_Series(src_ser);

    return (op == SYM_APPEND) ? 0 : dst_idx;
}
