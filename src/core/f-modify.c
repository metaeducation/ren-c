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

#include "sys-core.h"


//
//  Modify_Array: C
//
// Returns new dst_idx
//
REBLEN Modify_Array(
    REBARR *dst_arr,  // target
    REBLEN dst_idx,  // position
    enum Reb_Symbol sym,  // INSERT, APPEND, CHANGE
    const REBVAL *src_val,  // source
    REBLEN flags,  // AM_SPLICE, AM_PART, AM_LINE
    REBLEN part,  // dst to remove (CHANGE) or limit to grow (APPEND/INSERT)
    REBINT dups  // dup count of how many times to insert the src content
){
    assert(sym == SYM_INSERT or sym == SYM_CHANGE or sym == SYM_APPEND);

    REBLEN tail = ARR_LEN(dst_arr);

    const RELVAL *src_rel;
    REBSPC *specifier;

    if (IS_NULLED(src_val) and sym == SYM_CHANGE) {
        //
        // Tweak requests to CHANGE to a null to be a deletion; basically
        // what happens with an empty block.
        //
        flags |= AM_SPLICE;
        src_val = EMPTY_BLOCK;
    }

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
    //   so there's no element after the last item, but NEWLINE_AT_TAIL is set
    //   on the inserted array.
    //
    bool tail_newline = did (flags & AM_LINE);
    REBLEN ilen;

    // Check /PART, compute LEN:
    if (flags & AM_SPLICE) {
        REBCEL(const*) unescaped = VAL_UNESCAPED(src_val);
        assert(ANY_ARRAY_KIND(CELL_KIND(unescaped)));

        ilen = VAL_LEN_AT(unescaped);

        // Adjust length of insertion if changing /PART:
        if (sym != SYM_CHANGE and (flags & AM_PART)) {
            if (part < ilen)
                ilen = part;
        }

        if (not tail_newline) {
            const RELVAL *tail_cell = VAL_ARRAY_AT(unescaped) + ilen;
            if (IS_END(tail_cell)) {
                tail_newline = GET_ARRAY_FLAG(
                    VAL_ARRAY(src_val),
                    NEWLINE_AT_TAIL
                );
            }
            else if (ilen == 0)
                tail_newline = false;
            else
                tail_newline = GET_CELL_FLAG(tail_cell, NEWLINE_BEFORE);
        }

        // Are we modifying ourselves? If so, copy src_val block first:
        if (dst_arr == VAL_ARRAY(unescaped)) {
            REBARR *copy = Copy_Array_At_Extra_Shallow(
                VAL_ARRAY(unescaped),
                VAL_INDEX(unescaped),
                VAL_SPECIFIER(unescaped),
                0, // extra
                NODE_FLAG_MANAGED // !!! Worth it to not manage and free?
            );
            src_rel = ARR_HEAD(copy);
            specifier = SPECIFIED; // copy already specified it
        }
        else {
            src_rel = VAL_ARRAY_AT(unescaped); // skips by VAL_INDEX values
            specifier = VAL_SPECIFIER(unescaped);
        }
    }
    else {
        // use passed in RELVAL and specifier
        ilen = 1;
        src_rel = src_val;
        specifier = SPECIFIED; // it's a REBVAL, not a RELVAL, so specified
    }

    REBLEN size = cast(REBLEN, dups) * ilen;  // total to insert (dups is > 0)

    // If data is being tacked onto an array, beyond the newlines on the values
    // in that array there is also the chance that there's a newline tail flag
    // on the target, and the insertion is at the end.
    //
    bool head_newline =
        (dst_idx == ARR_LEN(dst_arr))
        and GET_ARRAY_FLAG(dst_arr, NEWLINE_AT_TAIL);

    if (sym != SYM_CHANGE) {
        // Always expand dst_arr for INSERT and APPEND actions:
        Expand_Series(SER(dst_arr), dst_idx, size);
    }
    else {
        if (size > part)
            Expand_Series(SER(dst_arr), dst_idx, size - part);
        else if (size < part and (flags & AM_PART))
            Remove_Series_Units(SER(dst_arr), dst_idx, part - size);
        else if (size + dst_idx > tail) {
            EXPAND_SERIES_TAIL(SER(dst_arr), size - (tail - dst_idx));
        }
    }

    tail = (sym == SYM_APPEND) ? 0 : size + dst_idx;

    REBLEN dup_index = 0;
    for (; dup_index < cast(REBLEN, dups); ++dup_index) {  // dups checked > 0
        REBLEN index = 0;
        for (; index < ilen; ++index, ++dst_idx) {
            Derelativize(
                ARR_HEAD(dst_arr) + dst_idx,
                src_rel + index,
                specifier
            );

            if (dup_index == 0 and index == 0 and head_newline) {
                SET_CELL_FLAG(ARR_HEAD(dst_arr) + dst_idx, NEWLINE_BEFORE);

                // The array flag is not cleared until the loop actually
                // makes a value that will carry on the bit.
                //
                CLEAR_ARRAY_FLAG(dst_arr, NEWLINE_AT_TAIL);
                continue;
            }

            if (dup_index > 0 and index == 0 and tail_newline) {
                SET_CELL_FLAG(ARR_HEAD(dst_arr) + dst_idx, NEWLINE_BEFORE);
            }
        }
    }

    // The above loop only puts on (dups - 1) NEWLINE_BEFORE flags.  The
    // last one might have to be the array flag if at tail.
    //
    if (tail_newline) {
        if (dst_idx == ARR_LEN(dst_arr))
            SET_ARRAY_FLAG(dst_arr, NEWLINE_AT_TAIL);
        else
            SET_CELL_FLAG(ARR_AT(dst_arr, dst_idx), NEWLINE_BEFORE);
    }

    if (flags & AM_LINE) {
        //
        // !!! Testing this heuristic: if someone adds a line to an array
        // with the /LINE flag explicitly, force the head element to have a
        // newline.  This allows `x: copy [] | append/line x [a b c]` to give
        // a more common result.  The head line can be removed easily.
        //
        SET_CELL_FLAG(ARR_HEAD(dst_arr), NEWLINE_BEFORE);
    }

    ASSERT_ARRAY(dst_arr);

    return tail;
}


//
//  Modify_String_Or_Binary: C
//
// This returns the index of the tail of the insertion.  The reason it does
// so is because the caller would have a hard time calculating that if the
// input series were FORM'd.
//
// It is possible to alias strings as binaries (or alias a binary as a string,
// but doing so flags the series with SERIES_FLAG_IS_STRING).  If a binary
// is aliased anywhere as a string, it must carry this flag--and once it does
// so, then all mutations must preserve the series content as valid UTF-8.
// That aliasing ability is why this routine is for both strings and binaries.
//
// While a BINARY! and an ANY-STRING! can alias the same series, the meaning
// of VAL_INDEX() is different.  So in addition to the detection of the
// SERIES_FLAG_IS_STRING on the series, we must know if dst is a BINARY!.
//
REBLEN Modify_String_Or_Binary(
    REBVAL *dst,  // ANY-STRING! or BINARY! value to modify
    enum Reb_Symbol sym,  // SYM_APPEND @ tail, SYM_INSERT/SYM_CHANGE @ index
    const REBVAL *src,  // ANY-VALUE! argument with content to inject
    REBFLGS flags,  // AM_PART, AM_LINE
    REBLEN part,  // dst to remove (CHANGE) or limit to grow (APPEND/INSERT)
    REBINT dups  // dup count of how many times to insert the src content
){
    assert(sym == SYM_INSERT or sym == SYM_CHANGE or sym == SYM_APPEND);

    ENSURE_MUTABLE(dst);  // note this also rules out ANY-WORD!s

    REBSER *dst_ser = VAL_SERIES_ENSURE_MUTABLE(dst);
    REBLEN dst_idx = VAL_INDEX(dst);
    REBSIZ dst_used = SER_USED(dst_ser);

    REBLEN dst_len_old = 0xDECAFBAD;  // only if IS_SER_STRING(dst_ser)
    REBSIZ dst_off;
    if (IS_BINARY(dst)) {  // check invariants up front even if NULL / no-op
        if (IS_SER_STRING(dst_ser)) {
            REBYTE at = *BIN_AT(dst_ser, dst_idx);
            if (Is_Continuation_Byte_If_Utf8(at))
                fail (Error_Bad_Utf8_Bin_Edit_Raw());
            dst_len_old = STR_LEN(STR(dst_ser));
        }
        dst_off = dst_idx;
    }
    else {
        assert(ANY_STRING(dst));
        assert(IS_SER_STRING(dst_ser));
        assert(not IS_STR_SYMBOL(STR(dst_ser)));  // would have been read-only

        dst_off = VAL_OFFSET_FOR_INDEX(dst, dst_idx);  // !!! review for speed
        dst_len_old = STR_LEN(STR(dst_ser));
    }

    if (IS_NULLED(src)) {  // no-op, unless CHANGE, where it means delete
        if (sym == SYM_APPEND)
            return 0;  // APPEND returns index at head
        else if (sym == SYM_INSERT)
            return dst_idx;  // INSERT returns index at insertion tail

        assert(sym == SYM_CHANGE);
        flags |= AM_SPLICE;
        src = EMPTY_TEXT;  // give same behavior as CHANGE to empty string
    }

    // For INSERT/PART and APPEND/PART
    //
    REBLEN limit;
    if (sym != SYM_CHANGE and (flags & AM_PART))
        limit = part;
    else
        limit = UINT32_MAX;

    if (limit == 0 or dups <= 0)
        return sym == SYM_APPEND ? 0 : dst_idx;

    // Now that we know there's actual work to do, we need `dst_idx` to speak
    // in terms of codepoints (if applicable)

    if (sym == SYM_APPEND or dst_off > dst_used) {
        dst_off = SER_USED(dst_ser);
        dst_idx = dst_len_old;
    }
    else if (IS_BINARY(dst) and IS_SER_STRING(dst_ser)) {
        dst_idx = STR_INDEX_AT(STR(dst_ser), dst_off);
    }

    // If the src is not an ANY-STRING!, then we need to create string data
    // from the value to use its content.
    //
    DECLARE_MOLD (mo);  // mo->series will be non-null if Push_Mold() run

    const REBYTE *src_ptr;  // start of utf-8 encoded data to insert
    REBLEN src_len_raw;  // length in codepoints (if dest is string)
    REBSIZ src_size_raw;  // size in bytes

    REBYTE src_byte;  // only used by BINARY! (mold buffer is UTF-8 legal)

    if (IS_ISSUE(src)) {  // characters store their encoding in their payload
        //
        // !!! We pass in an UNKNOWN for the limit of how long the input is
        // because currently /PART speaks in terms of the destination series.
        // However, if that were changed to /LIMIT then we would want to
        // be cropping the /PART of the input via passing a parameter here.
        //
        src_ptr = VAL_UTF8_LEN_SIZE_AT_LIMIT(
            &src_len_raw,
            &src_size_raw,
            src,
            UNKNOWN
        );

        if (IS_SER_STRING(dst_ser)) {
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
        ANY_STRING(src)
        and not IS_TAG(src)  // tags need `<` and `>` to render
    ){
        // !!! Branch is very similar to the one for ISSUE! above (merge?)
 
        // If Source == Destination we must prevent possible conflicts in
        // the memory regions being moved.  Clone the series just to be safe.
        //
        // !!! It may be possible to optimize special cases like append.
        //
        if (VAL_SERIES(dst) == VAL_SERIES(src))
            goto form;

        src_ptr = VAL_STRING_AT(src);

        // !!! We pass in an UNKNOWN for the limit of how long the input is
        // because currently /PART speaks in terms of the destination series.
        // However, if that were changed to /LIMIT then we would want to
        // be cropping the /PART of the input via passing a parameter here.
        //
        src_size_raw = VAL_SIZE_LIMIT_AT(&src_len_raw, src, UNKNOWN);
        if (not IS_SER_STRING(dst_ser))
            src_len_raw = src_size_raw;
    }
    else if (IS_INTEGER(src)) {
        if (not IS_BINARY(dst))
            goto form;  // e.g. `append "abc" 10` is "abc10"

        // otherwise `append #{123456} 10` is #{1234560A}, just the byte

        src_byte = VAL_UINT8(src);  // fails if out of range
        if (IS_SER_STRING(dst_ser) and src_byte >= 0x80)
            fail (Error_Bad_Utf8_Bin_Edit_Raw());

        src_ptr = &src_byte;
        src_len_raw = src_size_raw = 1;
    }
    else if (IS_BINARY(src)) {
        const REBBIN *bin = VAL_BINARY(src);
        REBLEN offset = VAL_INDEX(src);

        src_ptr = BIN_AT(bin, offset);
        src_size_raw = BIN_LEN(bin) - offset;

        if (not IS_SER_STRING(dst_ser)) {
            if (limit > 0 and limit < src_size_raw)
                src_size_raw = limit;  // /PART is in bytes for binary! dest
            src_len_raw = src_size_raw;
        }
        else {
            if (IS_SER_STRING(bin)) {  // guaranteed valid UTF-8
                const REBSTR *str = STR(bin);
                if (Is_Continuation_Byte_If_Utf8(*src_ptr))
                    fail (Error_Bad_Utf8_Bin_Edit_Raw());

                // !!! We could be more optimal here since we know it's valid
                // UTF-8 than walking characters up to the limit, like:
                //
                // `src_len_raw = STR_LEN(str) - STR_INDEX_AT(str, offset);`
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

                REBSIZ bytes_left = src_size_raw;
                const REBYTE *bp = src_ptr;
                for (; bytes_left > 0; --bytes_left, ++bp) {
                    REBUNI c = *bp;
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
                        break;  // Note: /PART is in codepoints
                }
            }
        }

        // We have to worry about conflicts and resizes if the source and
        // destination are the same.  Special cases like APPEND might be
        // optimizable here, but appending series to themselves is rare-ish.
        // Use the byte buffer.
        //
        if (bin == dst_ser) {
            SET_SERIES_LEN(BYTE_BUF, 0);
            EXPAND_SERIES_TAIL(BYTE_BUF, src_size_raw);
            memcpy(BIN_HEAD(BYTE_BUF), src_ptr, src_size_raw);
            src_ptr = BIN_HEAD(BYTE_BUF);
        }

        goto binary_limit_accounted_for;
    }
    else if (IS_BLOCK(src)) {
        //
        // !!! For APPEND and INSERT, the /PART should apply to *block* units,
        // and not character units from the generated string.

        if (IS_BINARY(dst)) {
            //
            // !!! R3-Alpha had the notion of joining a binary into a global
            // buffer that was cleared out and reused.  This was not geared
            // to be safe for threading.  It might be unified with the mold
            // buffer now that they are both byte-oriented...though there may
            // be some advantage to the mold buffer being UTF-8 only.
            //
            Join_Binary_In_Byte_Buf(src, -1);
            src_ptr = BIN_HEAD(BYTE_BUF);  // cleared each time
            src_len_raw = src_size_raw = BIN_LEN(BYTE_BUF);
        }
        else {
            Push_Mold(mo);

            // !!! The logic for append/insert/change on ANY-STRING! with a
            // BLOCK! has been to form them without reducing, and no spaces
            // between.  There is some rationale to this, though implications
            // for operations like TO TEXT! of a BLOCK! are unclear...
            //
            const RELVAL *item;
            for (item = VAL_ARRAY_AT(src); NOT_END(item); ++item)
                Form_Value(mo, item);
            goto use_mold_buffer;
        }
    }
    else { form:

        Push_Mold(mo);
        Mold_Or_Form_Value(mo, src, true);

        // Don't capture pointer until after mold (it may expand the buffer)

      use_mold_buffer:

        src_ptr = BIN_AT(SER(mo->series), mo->offset);
        src_size_raw = STR_SIZE(mo->series) - mo->offset;
        if (not IS_SER_STRING(dst_ser))
            src_len_raw = src_size_raw;
        else
            src_len_raw = STR_LEN(mo->series) - mo->index;
    }

    // Here we are accounting for a /PART where we know the source series
    // data is valid UTF-8.  (If the source were a BINARY!, where the /PART
    // counts in bytes, it would have jumped below here with limit set up.)
    //
    // !!! Bad first implementation; improve.
    //
    if (IS_SER_STRING(dst_ser)) {
        REBCHR(const*) t = cast(REBCHR(const*), src_ptr + src_size_raw);
        while (src_len_raw > limit) {
            t = BACK_STR(t);
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

    REBSIZ src_size_total;  // includes duplicates and newlines, if applicable
    REBLEN src_len_total;
    if (flags & AM_LINE) {
        src_size_total = (src_size_raw + 1) * dups;
        src_len_total = (src_len_raw + 1) * dups;
    }
    else {
        src_size_total = src_size_raw * dups;
        src_len_total = src_len_raw * dups;
    }

    REBBMK *bookmark = nullptr;
    if (IS_SER_STRING(dst_ser))
        bookmark = LINK(dst_ser).bookmarks;

    // For strings, we should have generated a bookmark in the process of this
    // modification in most cases where the size is notable.  If we had not,
    // we might add a new bookmark pertinent to the end of the insertion for
    // longer series.

    if (sym == SYM_APPEND or sym == SYM_INSERT) {  // always expands
        Expand_Series(dst_ser, dst_off, src_size_total);
        SET_SERIES_USED(dst_ser, dst_used + src_size_total);

        if (IS_SER_STRING(dst_ser)) {
            if (bookmark and BMK_INDEX(bookmark) > dst_idx) {  // only INSERT
                BMK_INDEX(bookmark) += src_len_total;
                BMK_OFFSET(bookmark) += src_size_total;
            }
            MISC(dst_ser).length = dst_len_old + src_len_total;
        }
    }
    else {  // CHANGE only expands if more content added than overwritten
        assert(sym == SYM_CHANGE);

        // Historical behavior: `change s: "abc" "d"` will yield S as `"dbc"`.
        //
        if (not (flags & AM_PART))
            part = src_len_total;

        REBLEN dst_len_at;
        REBSIZ dst_size_at;
        if (IS_SER_STRING(dst_ser)) {
            if (IS_BINARY(dst)) {
                dst_size_at = VAL_LEN_AT(dst);  // byte count
                dst_len_at = STR_INDEX_AT(STR(dst_ser), dst_size_at);
            }
            else
                dst_size_at = VAL_SIZE_LIMIT_AT(&dst_len_at, dst, UNKNOWN);
        }
        else {
            dst_len_at = VAL_LEN_AT(dst);
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

        REBSIZ part_size;
        if (IS_SER_STRING(dst_ser)) {
            if (IS_BINARY(dst)) {
                //
                // The calculations on the new length depend on `part` being
                // in terms of codepoint count.  Transform it from byte count,
                // and also be sure it's a legitimate codepoint boundary and
                // not splitting a codepoint's bytes.
                //
                if (part > dst_size_at) {  // can use STR_LEN() from above
                    part = dst_len_at;
                    part_size = dst_size_at;
                }
                else {  // count how many codepoints are in the `part`
                    part_size = part;
                    REBCHR(*) cp = BIN_AT(dst_ser, dst_off);
                    REBCHR(*) pp = BIN_AT(dst_ser, dst_off + part_size);
                    if (Is_Continuation_Byte_If_Utf8(*pp))
                        fail (Error_Bad_Utf8_Bin_Edit_Raw());

                    part = 0;
                    for (; cp != pp; cp = NEXT_STR(cp))
                        ++part;
                }
            }
            else {
                if (part > dst_len_at) {  // can use STR_LEN() from above
                    part = dst_len_at;
                    part_size = dst_size_at;
                }
                else {
                    REBLEN check;
                    part_size = VAL_SIZE_LIMIT_AT(&check, dst, part);
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
            Expand_Series(
                dst_ser,
                dst_off,
                src_size_total - part_size
            );
            SET_SERIES_USED(dst_ser, dst_used + src_size_total - part_size);
        }
        else if (part_size > src_size_total) {
            //
            // We're taking out more bytes than we're inserting.  Slide left.
            //
            Remove_Series_Units(
                dst_ser,
                dst_off,
                part_size - src_size_total
            );
            SET_SERIES_USED(dst_ser, dst_used + src_size_total - part_size);
        }
        else {
            // staying the same size (change "abc" "-" => "-bc")
        }

        // CHANGE can do arbitrary changes to what index maps to what offset
        // in the region of interest.  The manipulations here would be
        // complicated--but just assume that the start of the change is as
        // good a cache as any to be relevant for the next operation.
        //
        if (IS_SER_STRING(dst_ser)) {
            if (bookmark and BMK_INDEX(bookmark) > dst_idx) {
                BMK_INDEX(bookmark) = dst_idx;
                BMK_OFFSET(bookmark) = dst_off;
            }
            MISC(dst_ser).length = dst_len_old + src_len_total - part;
        }
    }

    // Since the series may be expanded, its pointer could change...so this
    // can't be done up front at the top of this routine.
    //
    REBYTE *dst_ptr = SER_AT(REBYTE, dst_ser, dst_off);

    REBLEN d;
    for (d = 0; d < cast(REBLEN, dups); ++d) {  // dups checked above as > 0
        memcpy(dst_ptr, src_ptr, src_size_raw);
        dst_ptr += src_size_raw;

        if (flags & AM_LINE) {  // line is not actually in inserted material
            *dst_ptr = '\n';
            ++dst_ptr;
        }
    }

    if (mo->series != nullptr)  // ...a Push_Mold() happened
        Drop_Mold(mo);

    // !!! Should BYTE_BUF's memory be reclaimed also (or should it be
    // unified with the mold buffer?)

    if (bookmark) {
        REBSTR *dst_str = STR(dst_ser);
        if (BMK_INDEX(bookmark) > STR_LEN(dst_str)) {  // past active
            assert(sym == SYM_CHANGE);  // only change removes material
            Free_Bookmarks_Maybe_Null(dst_str);
        }
        else {
          #if defined(DEBUG_BOOKMARKS_ON_MODIFY)
            Check_Bookmarks_Debug(dst_str);
          #endif

            if (STR_LEN(dst_str) < sizeof(REBVAL))  // not kept if small
                Free_Bookmarks_Maybe_Null(dst_str);
        }
    }

    ASSERT_SERIES_TERM(dst_ser);

    if (sym == SYM_APPEND)
        return 0;

    if (IS_BINARY(dst))
        return dst_off + src_size_total;

    return dst_idx + src_len_total;
}
