//
//  File: %f-modify.c
//  Summary: "ANY-SERIES? modification (insert, append, change)"
//  Section: functional
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2024 Ren-C Open Source Contributors
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

#include "sys-core.h"


//
//  Modify_Array: C
//
// Returns new dst_idx
//
REBLEN Modify_Array(
    Array* dst_arr,  // target
    REBLEN dst_idx,  // position
    SymId op,  // INSERT, APPEND, CHANGE
    const Value* src_val,  // source
    REBLEN flags,  // AM_SPLICE, AM_PART, AM_LINE
    REBLEN part,  // dst to remove (CHANGE) or limit to grow (APPEND or INSERT)
    REBINT dups  // dup count of how many times to insert the src content
){
    assert(op == SYM_INSERT or op == SYM_CHANGE or op == SYM_APPEND);

    REBLEN tail_idx = Array_Len(dst_arr);

    const Element* src_rel;

    if (op == SYM_CHANGE and Is_Void(src_val)) {
        flags |= AM_SPLICE;
        src_val = EMPTY_BLOCK;  // CHANGE to void acts same as with empty block
    }
    else if (Is_Void(src_val) or dups <= 0) {
        //
        // If they are effectively asking for "no action" then all we have
        // to do is return the natural index result for the operation.
        // (APPEND will return 0, INSERT the tail of the insertion...)
        //
        return (op == SYM_APPEND) ? 0 : dst_idx;
    }

    if (op == SYM_APPEND or dst_idx > tail_idx)
        dst_idx = tail_idx;

    // Each dup being inserted need a newline signal after it if:
    //
    // * The user explicitly invokes the /LINE refinement (AM_LINE flag)
    // * It's a spliced insertion and there's a NEWLINE_BEFORE flag on the
    //   element *after* the last item in the dup
    // * It's a spliced insertion and there dup goes to the end of the array
    //   so there's no element after the last item, but NEWLINE_AT_TAIL is set
    //   on the inserted array.
    //
    bool tail_newline = did (flags & AM_LINE);
    REBLEN ilen;

    // Check :PART, compute LEN:
    if (flags & AM_SPLICE) {
        assert(Any_List(src_val));

        REBLEN len_at = Cell_Series_Len_At(src_val);
        ilen = len_at;

        // Adjust length of insertion if changing :PART
        if (op != SYM_CHANGE and (flags & AM_PART)) {
            if (part < ilen)
                ilen = part;
        }

        if (not tail_newline) {
            if (ilen == len_at) {
                tail_newline = Get_Subclass_Flag(
                    ARRAY,
                    Cell_Array(src_val),
                    NEWLINE_AT_TAIL
                );
            }
            else if (ilen == 0)
                tail_newline = false;
            else {
                const Cell* tail_cell
                    = Cell_List_Item_At(src_val) + ilen;
                tail_newline = Get_Cell_Flag(tail_cell, NEWLINE_BEFORE);
            }
        }

        // Are we modifying ourselves? If so, copy src_val block first:
        if (dst_arr == Cell_Array(src_val)) {
            Array* copy = Copy_Array_At_Extra_Shallow(
                Cell_Array(src_val),
                VAL_INDEX(src_val),
                0, // extra
                NODE_FLAG_MANAGED // !!! Worth it to not manage and free?
            );
            src_rel = Array_Head(copy);
        }
        else {
            src_rel = Cell_List_At(nullptr, src_val);  // may be tail
        }
    }
    else {
        // use passed in Cell
        ilen = 1;
        assert(Not_Antiform(src_val));
        src_rel = c_cast(Element*, src_val);
    }

    REBLEN size = dups * ilen;  // total to insert (dups is > 0)

    // If data is being tacked onto an array, beyond the newlines on the values
    // in that array there is also the chance that there's a newline tail flag
    // on the target, and the insertion is at the end.
    //
    bool head_newline =
        (dst_idx == Array_Len(dst_arr))
        and Get_Array_Flag(dst_arr, NEWLINE_AT_TAIL);

    if (op != SYM_CHANGE) {
        // Always expand dst_arr for INSERT and APPEND actions:
        Expand_Flex(dst_arr, dst_idx, size);
    }
    else {
        if (size > part)
            Expand_Flex(dst_arr, dst_idx, size - part);
        else if (size < part and (flags & AM_PART))
            Remove_Flex_Units(dst_arr, dst_idx, part - size);
        else if (size + dst_idx > tail_idx) {
            Expand_Flex_Tail(dst_arr, size - (tail_idx - dst_idx));
        }
    }

    tail_idx = (op == SYM_APPEND) ? 0 : size + dst_idx;

    REBLEN dup_index = 0;
    for (; dup_index < dups; ++dup_index) {  // dups checked > 0
        REBLEN index = 0;
        for (; index < ilen; ++index, ++dst_idx) {
            Copy_Cell(
                Array_Head(dst_arr) + dst_idx,
                src_rel + index
            );

            if (dup_index == 0 and index == 0 and head_newline) {
                Set_Cell_Flag(Array_Head(dst_arr) + dst_idx, NEWLINE_BEFORE);

                // The array flag is not cleared until the loop actually
                // makes a value that will carry on the bit.
                //
                Clear_Array_Flag(dst_arr, NEWLINE_AT_TAIL);
                continue;
            }

            if (dup_index > 0 and index == 0 and tail_newline) {
                Set_Cell_Flag(Array_Head(dst_arr) + dst_idx, NEWLINE_BEFORE);
            }
        }
    }

    // The above loop only puts on (dups - 1) NEWLINE_BEFORE flags.  The
    // last one might have to be the array flag if at tail.
    //
    if (tail_newline) {
        if (dst_idx == Array_Len(dst_arr))
            Set_Array_Flag(dst_arr, NEWLINE_AT_TAIL);
        else
            Set_Cell_Flag(Array_At(dst_arr, dst_idx), NEWLINE_BEFORE);
    }

    if (flags & AM_LINE) {
        //
        // !!! Testing this heuristic: if someone adds a line to a list
        // with the /LINE flag explicitly, force the head element to have a
        // newline.  This allows `x: copy [] | append:line x [a b c]` to give
        // a more common result.  The head line can be removed easily.
        //
        Set_Cell_Flag(Array_Head(dst_arr), NEWLINE_BEFORE);
    }

  #if DEBUG_POISON_FLEX_TAILS
    if (Get_Flex_Flag(dst_arr, DYNAMIC))
        Poison_Cell(Array_Tail(dst_arr));
  #endif

    Assert_Array(dst_arr);

    return tail_idx;
}


//
//  Modify_String_Or_Binary: C
//
// This returns the index of the tail of the insertion.  The reason it does
// so is because the caller would have a hard time calculating that if the
// input Flex were FORM'd.
//
// It is possible to alias ANY-STRING? as BINARY! (or alias a binary as string,
// but doing so marks the Flex with FLEX_FLAG_IS_STRING).  If a Blob's Binary
// is aliased anywhere as a String Flex, it must carry this flag--and once it
// does so, then all mutations must preserve the Flex content as valid UTF-8.
// That aliasing ability is why this routine is for both string and binary.
//
// While a BINARY! and an ANY-STRING? can alias the same Flex, the meaning
// of VAL_INDEX() is different.  So in addition to the detection of the
// FLEX_FLAG_IS_STRING on the Flex, we must know if dst is a BINARY!.
//
REBLEN Modify_String_Or_Binary(
    Value* dst,  // ANY-STRING? or BINARY! value to modify
    SymId op,  // SYM_APPEND @ tail, SYM_INSERT or SYM_CHANGE @ index
    const Value* src,  // argument with content to inject
    Flags flags,  // AM_PART, AM_LINE
    REBLEN part,  // dst to remove (CHANGE) or limit to grow (APPEND or INSERT)
    REBINT dups  // dup count of how many times to insert the src content
){
    assert(op == SYM_INSERT or op == SYM_CHANGE or op == SYM_APPEND);

    Ensure_Mutable(dst);  // note this also rules out ANY-WORD?s

    Binary* dst_flex = cast(Binary*, Cell_Flex_Ensure_Mutable(dst));
    assert(not Is_Stub_Symbol(dst_flex));  // would be immutable

    REBLEN dst_idx = VAL_INDEX(dst);
    Size dst_used = Flex_Used(dst_flex);

    REBLEN dst_len_old = 0xDECAFBAD;  // only if IS_SER_STRING(dst_ser)
    Size dst_off;
    if (Is_Binary(dst)) {  // check invariants up front even if NULL / no-op
        if (Is_Stub_String(dst_flex)) {
            Byte at = *Binary_At(dst_flex, dst_idx);
            if (Is_Continuation_Byte(at))
                fail (Error_Bad_Utf8_Bin_Edit_Raw());
            dst_len_old = String_Len(cast(String*, dst_flex));
        }
        dst_off = dst_idx;
    }
    else {
        assert(Any_String(dst));

        dst_off = VAL_BYTEOFFSET_FOR_INDEX(dst, dst_idx);  // !!! review speed
        dst_len_old = String_Len(cast(String*, dst_flex));
    }

    if (Is_Void(src)) {  // no-op, unless CHANGE, where it means delete
        if (op == SYM_APPEND)
            return 0;  // APPEND returns index at head
        else if (op == SYM_INSERT)
            return dst_idx;  // INSERT returns index at insertion tail

        assert(op == SYM_CHANGE);
        flags |= AM_SPLICE;
        src = EMPTY_TEXT;  // give same behavior as CHANGE to empty string
    }

    // For INSERT:PART and APPEND:PART
    //
    REBLEN limit;
    if (op != SYM_CHANGE and (flags & AM_PART))
        limit = part;
    else
        limit = UINT32_MAX;

    if (limit == 0 or dups <= 0)
        return op == SYM_APPEND ? 0 : dst_idx;

    // Now that we know there's actual work to do, we need `dst_idx` to speak
    // in terms of codepoints (if applicable)

    if (op == SYM_APPEND or dst_off > dst_used) {
        dst_off = Flex_Used(dst_flex);
        dst_idx = dst_len_old;
    }
    else if (Is_Binary(dst) and Is_Stub_String(dst_flex)) {
        dst_idx = String_Index_At(cast(String*, dst_flex), dst_off);
    }

    // If the src is not an ANY-STRING?, then we need to create string data
    // from the value to use its content.
    //
    DECLARE_MOLDER (mo);  // mo->string will be non-null if Push_Mold() run

    const Byte* src_ptr;  // start of utf-8 encoded data to insert
    REBLEN src_len_raw;  // length in codepoints (if dest is string)
    Size src_size_raw;  // size in bytes

    Byte src_byte;  // only used by BINARY! (mold buffer is UTF-8 legal)

    if (Is_Issue(src)) {  // characters store their encoding in their payload
        //
        // !!! We pass in UNLIMITED for the limit of how long the input is
        // because currently :PART speaks in terms of the destination series.
        // However, if that were changed to /LIMIT then we would want to
        // be cropping the :PART of the input via passing a parameter here.
        //
        src_ptr = Cell_Utf8_Len_Size_At_Limit(
            &src_len_raw,
            &src_size_raw,
            src,
            UNLIMITED
        );

        if (Is_Stub_String(dst_flex)) {
            if (src_len_raw == 0)
                fail (Error_Illegal_Zero_Byte_Raw());  // no '\0' in strings
        }
        else {
            if (src_len_raw == 0)
                src_len_raw = src_size_raw = 1;  // Use the '\0' null term
            else
                src_len_raw = src_size_raw;  // binary counts length in bytes
        }
    }
    else if (
        Any_String(src)
        and not Is_Tag(src)  // tags need `<` and `>` to render
    ){
        // !!! Branch is very similar to the one for ISSUE! above (merge?)

        // If Source == Destination we must prevent possible conflicts in
        // the memory regions being moved.  Clone the Flex just to be safe.
        //
        // !!! It may be possible to optimize special cases like append.
        //
        if (Cell_Flex(dst) == Cell_Flex(src))
            goto form;

        src_ptr = Cell_String_At(src);

        // !!! We pass in UNLIMITED for the limit of how long the input is
        // because currently :PART speaks in terms of the destination series.
        // However, if that were changed to :LIMIT then we would want to
        // be cropping the :PART of the input via passing a parameter here.
        //
        src_size_raw = Cell_String_Size_Limit_At(&src_len_raw, src, UNLIMITED);
        if (not Is_Stub_String(dst_flex))
            src_len_raw = src_size_raw;
    }
    else if (Is_Integer(src)) {
        if (not Is_Binary(dst))
            goto form;  // e.g. `append "abc" 10` is "abc10"

        // otherwise `append #{123456} 10` is #{1234560A}, just the byte

        src_byte = VAL_UINT8(src);  // fails if out of range
        if (Is_Stub_String(dst_flex) and src_byte >= 0x80)
            fail (Error_Bad_Utf8_Bin_Edit_Raw());

        src_ptr = &src_byte;
        src_len_raw = src_size_raw = 1;
    }
    else if (Is_Binary(src)) {
        const Binary* b = Cell_Binary(src);
        REBLEN offset = VAL_INDEX(src);

        src_ptr = Binary_At(b, offset);
        src_size_raw = Binary_Len(b) - offset;

        if (not Is_Stub_String(dst_flex)) {
            if (limit > 0 and limit < src_size_raw)
                src_size_raw = limit;  // :PART is byte count for binary! dest
            src_len_raw = src_size_raw;
        }
        else {
            if (Is_Stub_String(b)) {  // guaranteed valid UTF-8
                const String* str = c_cast(String*, b);
                if (Is_Continuation_Byte(*src_ptr))
                    fail (Error_Bad_Utf8_Bin_Edit_Raw());

                // !!! We could be more optimal here since we know it's valid
                // UTF-8 than walking characters up to the limit, like:
                //
                // `src_len_raw = String_Len(str) - String_Index_At(str, offset);`
                //
                // But for simplicity just use the same branch that unverified
                // binaries do for now.  This code can be optimized when the
                // functionality has been proven for a while.
                //
                UNUSED(str);
                goto unverified_utf8_src_binary;
            }
            else {
              unverified_utf8_src_binary:
                //
                // The binary may be invalid UTF-8.  We don't actually need
                // to worry about the *entire* binary, just the part we are
                // adding (whereas AS has to worry about the *whole* binary
                // for aliasing, since BACK and HEAD are still possible)
                //
                src_len_raw = 0;

                Size bytes_left = src_size_raw;
                const Byte* bp = src_ptr;
                for (; bytes_left > 0; --bytes_left, ++bp) {
                    Codepoint c = *bp;
                    if (c < 0x80) {  // ASCII, just check for 0 bytes
                        if (c == '\0')
                            fail (Error_Illegal_Zero_Byte_Raw());
                    }
                    else {
                        bp = Back_Scan_UTF8_Char(&c, bp, &bytes_left);
                        if (not bp)  // !!! Should Back_Scan() fail?
                            fail (Error_Bad_Utf8_Bin_Edit_Raw());
                    }
                    ++src_len_raw;

                    if (limit == src_len_raw)
                        break;  // Note: :PART is count in codepoints
                }
            }
        }

        // We have to worry about conflicts and resizes if the source and
        // destination are the same.  Special cases like APPEND might be
        // optimizable here, but appending series to themselves is rare-ish.
        // Use the byte buffer.
        //
        if (b == dst_flex) {
            Set_Flex_Len(BYTE_BUF, 0);
            Expand_Flex_Tail(BYTE_BUF, src_size_raw);
            memcpy(Binary_Head(BYTE_BUF), src_ptr, src_size_raw);
            src_ptr = Binary_Head(BYTE_BUF);
        }

        goto binary_limit_accounted_for;
    }
    else if (Is_Group(src)) {
        //
        // !!! For APPEND and INSERT, the :PART should apply to *block* units,
        // and not character units from the generated string.

        if (Is_Binary(dst)) {
            //
            // !!! R3-Alpha had the notion of joining a binary into a global
            // buffer that was cleared out and reused.  This was not geared
            // to be safe for threading.  It might be unified with the mold
            // buffer now that they are both byte-oriented...though there may
            // be some advantage to the mold buffer being UTF-8 only.
            //
            Join_Binary_In_Byte_Buf(src, -1);
            src_ptr = Binary_Head(BYTE_BUF);  // cleared each time
            src_len_raw = src_size_raw = Binary_Len(BYTE_BUF);
        }
        else {
            Push_Mold(mo);

            // !!! The logic for APPEND or INSERT or CHAINGE on ANY-STRING? of
            // BLOCK! has been to form them without reducing, and no spaces
            // between.  There is some rationale to this, though implications
            // for operations like TO TEXT! of a BLOCK! are unclear...
            //
            const Element* item_tail;
            const Element* item = Cell_List_At(&item_tail, src);
            for (; item != item_tail; ++item)
                Form_Element(mo, item);
            goto use_mold_buffer;
        }
    }
    else { form:

        Push_Mold(mo);
        Mold_Or_Form_Element(mo, cast(const Element*, src), true);

        // Don't capture pointer until after mold (it may expand the buffer)

      use_mold_buffer:

        src_ptr = Binary_At(mo->string, mo->base.size);
        src_size_raw = String_Size(mo->string) - mo->base.size;
        if (Is_Stub_String(dst_flex))
            src_len_raw = String_Len(mo->string) - mo->base.index;
        else
            src_len_raw = src_size_raw;
    }

    // Here we are accounting for a :PART where we know the source series
    // data is valid UTF-8.  (If the source were a BINARY!, where the :PART
    // counts in bytes, it would have jumped below here with limit set up.)
    //
    // !!! Bad first implementation; improve.
    //
    if (Is_Stub_String(dst_flex)) {
        Utf8(const*) t = cast(Utf8(const*), src_ptr + src_size_raw);
        while (src_len_raw > limit) {
            t = Step_Back_Codepoint(t);
            --src_len_raw;
        }
        src_size_raw = t - src_ptr;  // src_len_raw now equals limit
    }
    else {  // copying valid UTF-8 data possibly partially in bytes (!)
        if (src_size_raw > limit)
            src_size_raw = limit;
        src_len_raw = src_size_raw;
    }

  binary_limit_accounted_for: ;  // needs ; (next line is declaration)

    Size src_size_total;  // includes duplicates and newlines, if applicable
    REBLEN src_len_total;
    if (flags & AM_LINE) {
        src_size_total = (src_size_raw + 1) * dups;
        src_len_total = (src_len_raw + 1) * dups;
    }
    else {
        src_size_total = src_size_raw * dups;
        src_len_total = src_len_raw * dups;
    }

  //=//// BELOW THIS LINE, BE CAREFUL WITH BOOKMARK-USING ROUTINES //////=//

    // We extract the destination's bookmarks for updating.  This may conflict
    // with other updating functions.  Be careful not to use any of the
    // functions like Cell_Utf8_Size_At() etc. that leverage bookmarks after
    // the extraction occurs.

    BookmarkList* book = nullptr;

    // For strings, we should have generated a bookmark in the process of this
    // modification in most cases where the size is notable.  If we had not,
    // we might add a new bookmark pertinent to the end of the insertion for
    // longer series.

    if (op == SYM_APPEND or op == SYM_INSERT) {  // always expands
        Expand_Flex(dst_flex, dst_off, src_size_total);
        Set_Flex_Used(dst_flex, dst_used + src_size_total);

        if (Is_Stub_String(dst_flex)) {
            book = LINK(Bookmarks, dst_flex);

            if (book and BMK_INDEX(book) > dst_idx) {  // only INSERT
                BMK_INDEX(book) += src_len_total;
                BMK_OFFSET(book) += src_size_total;
            }
            dst_flex->misc.length = dst_len_old + src_len_total;
        }
    }
    else {  // CHANGE only expands if more content added than overwritten
        assert(op == SYM_CHANGE);

        // Historical behavior: `change s: "abc" "d"` will yield S as `"dbc"`.
        //
        if (not (flags & AM_PART))
            part = src_len_total;

        REBLEN dst_len_at;
        Size dst_size_at;
        if (Is_Stub_String(dst_flex)) {
            if (Is_Binary(dst)) {
                dst_size_at = Cell_Series_Len_At(dst);  // byte count
                dst_len_at = String_Index_At(
                    cast(String*, dst_flex),
                    dst_size_at
                );
            }
            else
                dst_size_at = Cell_String_Size_Limit_At(
                    &dst_len_at,
                    dst,
                    UNLIMITED
                );

            // Note: above functions may update the bookmarks --^
            //
            book = LINK(Bookmarks, dst_flex);
        }
        else {
            dst_len_at = Cell_Series_Len_At(dst);
            dst_size_at = dst_len_at;
        }

        // We are overwriting codepoints where the source codepoint sizes and
        // the destination codepoint sizes may be different.  Hence if we
        // were changing a four-codepoint sequence where all are 1 byte with
        // a single-codepoint sequence with a 4-byte codepoint, you get:
        //
        //     src_len == 1
        //     dst_len_at == 4
        //     src_size_total == 4
        //     dst_size_at == 4
        //
        // It deceptively seems there's enough capacity.  But since only one
        // codepoint is being overwritten (with a larger one), three bytes
        // have to be moved safely out of the way before being overwritten.

        Size part_size;
        if (Is_Stub_String(dst_flex)) {
            if (Is_Binary(dst)) {
                //
                // The calculations on the new length depend on `part` being
                // in terms of codepoint count.  Transform it from byte count,
                // and also be sure it's a legitimate codepoint boundary and
                // not splitting a codepoint's bytes.
                //
                if (part > dst_size_at) {  // can use String_Len() from above
                    part = dst_len_at;
                    part_size = dst_size_at;
                }
                else {  // count how many codepoints are in the `part`
                    part_size = part;
                    Utf8(*) cp = cast(Utf8(*), Binary_At(dst_flex, dst_off));
                    Utf8(*) pp = cast(Utf8(*),
                        Binary_At(dst_flex, dst_off + part_size)
                    );
                    if (Is_Continuation_Byte(*cast(Byte*, pp)))
                        fail (Error_Bad_Utf8_Bin_Edit_Raw());

                    part = 0;
                    for (; cp != pp; cp = Skip_Codepoint(cp))
                        ++part;
                }
            }
            else {
                if (part > dst_len_at) {  // can use String_Len() from above
                    part = dst_len_at;
                    part_size = dst_size_at;
                }
                else {
                    REBLEN check;  // v-- !!! This call uses bookmark, review
                    part_size = Cell_String_Size_Limit_At(&check, dst, &part);
                    assert(check == part);
                    UNUSED(check);
                }
            }
        }
        else {  // Just a non-aliased binary; keep the part in bytes
            if (part > dst_size_at) {
                part = dst_size_at;
                part_size = dst_size_at;
            }
            else
                part_size = part;
        }

        if (src_size_total > part_size) {
            //
            // We're adding more bytes than we're taking out.  Expand.
            //
            Expand_Flex(
                dst_flex,
                dst_off,
                src_size_total - part_size
            );
            Set_Flex_Used(dst_flex, dst_used + src_size_total - part_size);
        }
        else if (part_size > src_size_total) {
            //
            // We're taking out more bytes than we're inserting.  Slide left.
            //
            Remove_Flex_Units(
                dst_flex,
                dst_off,
                part_size - src_size_total
            );
            Set_Flex_Used(dst_flex, dst_used + src_size_total - part_size);
        }
        else {
            // staying the same size (change "abc" "-" => "-bc")
        }

        // CHANGE can do arbitrary changes to what index maps to what offset
        // in the region of interest.  The manipulations here would be
        // complicated--but just assume that the start of the change is as
        // good a cache as any to be relevant for the next operation.
        //
        if (Is_Stub_String(dst_flex)) {
            book = LINK(Bookmarks, dst_flex);

            if (book and BMK_INDEX(book) > dst_idx) {
                BMK_INDEX(book) = dst_idx;
                BMK_OFFSET(book) = dst_off;
            }
            dst_flex->misc.length = dst_len_old + src_len_total - part;
        }
    }

    // Since the Flex may be expanded, its pointer could change...so this
    // can't be done up front at the top of this routine.
    //
    Byte* dst_ptr = Flex_At(Byte, dst_flex, dst_off);

    REBLEN d;
    for (d = 0; d < dups; ++d) {  // dups checked above as > 0
        memcpy(dst_ptr, src_ptr, src_size_raw);
        dst_ptr += src_size_raw;

        if (flags & AM_LINE) {  // line is not actually in inserted material
            *dst_ptr = '\n';
            ++dst_ptr;
        }
    }

    if (mo->string != nullptr)  // ...a Push_Mold() happened
        Drop_Mold(mo);

    // !!! Should BYTE_BUF's memory be reclaimed also (or should it be
    // unified with the mold buffer?)

    if (book) {
        String* dst_str = cast(String*, dst_flex);
        if (BMK_INDEX(book) > String_Len(dst_str)) {  // past active
            assert(op == SYM_CHANGE);  // only change removes material
            Free_Bookmarks_Maybe_Null(dst_str);
        }
        else {
          #if DEBUG_BOOKMARKS_ON_MODIFY
            Check_Bookmarks_Debug(dst_str);
          #endif

            if (String_Len(dst_str) < Size_Of(Cell))  // not kept if small
                Free_Bookmarks_Maybe_Null(dst_str);
        }
    }

    // !!! Set_Flex_Used() now corrupts the terminating byte, which notices
    // problems when it's not synchronized.  Review why the above code does
    // not always produce a legitimate termination.
    //
    Term_Flex_If_Necessary(dst_flex);

    if (op == SYM_APPEND)
        return 0;

    if (Is_Binary(dst))
        return dst_off + src_size_total;

    return dst_idx + src_len_total;
}
