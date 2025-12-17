//
//  file: %f-modify.c
//  summary: "ANY-SERIES? modification (insert, append, change)"
//  section: functional
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2025 Ren-C Open Source Contributors
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
// A. These are service routines called by the native functions that implement
//    APPEND, INSERT, and CHANGE.  They do not do the "front-end" work of
//    checking for things like zero or negative dup counts, and they do not
//    assume meanings for things like null or void.  If you need that kind of
//    handling you should go through the native functions.
//

#include "sys-core.h"


//
//  Modify_List: C
//
Result(None) Modify_List(
    Element* list,  // target
    ModifyState op,  // INSERT, APPEND, CHANGE
    const Stable* v,  // source
    REBLEN flags,  // AM_LINE
    REBLEN part,  // dst to remove (CHANGE) or limit to grow (APPEND or INSERT)
    Count dups  // dup count of how many times to insert the src content
){
    assert(
        op == ST_MODIFY_APPEND
        or op == ST_MODIFY_INSERT
        or op == ST_MODIFY_CHANGE
    );
    assert(dups > 0);  // use native entry points for "weird" cases [A]
    assert(part >= 0);
    assert(not Is_Antiform(v) or Is_Splice(v));

    Source* array = Cell_Array_Ensure_Mutable(list);
    REBLEN index = SERIES_INDEX_UNBOUNDED(list);  // !!! bounded?
    REBLEN tail = Array_Len(array);

    if (op == ST_MODIFY_APPEND or index > tail)
        index = tail;

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
    Length splice_len;

    const Element* src;

    if (Is_Splice(v)) {  // Check :PART, compute LEN:
        Length len_at = Series_Len_At(v);
        splice_len = len_at;

        // Adjust length of insertion if APPEND or INSERT :PART
        if (op != ST_MODIFY_CHANGE) {
            if (part < splice_len)
                splice_len = part;
        }

        if (not tail_newline) {
            if (splice_len == len_at) {
                tail_newline = Get_Flavor_Flag(
                    SOURCE,
                    Cell_Array(v),
                    NEWLINE_AT_TAIL
                );
            }
            else if (splice_len == 0)
                tail_newline = false;
            else {
                const Cell* splice_tail = List_Item_At(v) + splice_len;
                tail_newline = Get_Cell_Flag(splice_tail, NEWLINE_BEFORE);
            }
        }

        // Are we modifying ourselves? If so, copy V's array first:
        if (array == Cell_Array(v)) {
            Array* copy = Copy_Array_At_Extra_Shallow(
                STUB_MASK_MANAGED_SOURCE,  // !!! or, don't manage and free?
                Cell_Array(v),
                Series_Index(v),
                0 // extra
            );
            src = Array_Head(copy);
        }
        else {
            src = List_At(nullptr, v);  // may be tail
        }
    }
    else {  // use passed in Cell
        splice_len = 1;
        src = Known_Element(v);
    }

    Length expansion = dups * splice_len;  // total to insert (dups is > 0)

    // If data is being tacked onto an array, beyond the newlines on the values
    // in that array there is also the chance that there's a newline tail flag
    // on the target, and the insertion is at the end.
    //
    bool head_newline =
        (index == Array_Len(array))
        and Get_Source_Flag(array, NEWLINE_AT_TAIL);

    if (op != ST_MODIFY_CHANGE) {
        // Always expand array for INSERT and APPEND actions:
        trap (
           Expand_Flex_At_Index_And_Update_Used(array, index, expansion)
        );
    }
    else {
        if (expansion > part) {
            trap (
              Expand_Flex_At_Index_And_Update_Used(
                array, index, expansion - part
            ));
        }
        else if (expansion < part)
            Remove_Flex_Units_And_Update_Used(array, index, part - expansion);
        else if (expansion + index > tail) {
            trap (
              Expand_Flex_Tail_And_Update_Used(
                array, expansion - (tail - index)
            ));
        }
    }

    tail = (op == ST_MODIFY_APPEND) ? 0 : index + expansion;

    REBLEN dup_index = 0;
    for (; dup_index < dups; ++dup_index) {  // dups checked > 0
        REBLEN i = 0;
        for (; i < splice_len; ++i, ++index) {
            Copy_Cell(
                Array_Head(array) + index,
                src + i
            );

            if (dup_index == 0 and i == 0 and head_newline) {
                Set_Cell_Flag(Array_Head(array) + index, NEWLINE_BEFORE);

                // The array flag is not cleared until the loop actually
                // makes a value that will carry on the bit.
                //
                Clear_Source_Flag(array, NEWLINE_AT_TAIL);
                continue;
            }

            if (dup_index > 0 and i == 0 and tail_newline) {
                Set_Cell_Flag(Array_Head(array) + index, NEWLINE_BEFORE);
            }
        }
    }

    // The above loop only puts on (dups - 1) NEWLINE_BEFORE flags.  The
    // last one might have to be the array flag if at tail.
    //
    if (tail_newline) {
        if (index == Array_Len(array))
            Set_Source_Flag(array, NEWLINE_AT_TAIL);
        else
            Set_Cell_Flag(Array_At(array, index), NEWLINE_BEFORE);
    }

    if (flags & AM_LINE) {
        //
        // !!! Testing this heuristic: if someone adds a line to a list
        // with the :LINE flag explicitly, force the head element to have a
        // newline.  This allows `x: copy [] | append:line x [a b c]` to give
        // a more common result.  The head line can be removed easily.
        //
        Set_Cell_Flag(Array_Head(array), NEWLINE_BEFORE);
    }

  #if DEBUG_POISON_FLEX_TAILS
    if (Get_Stub_Flag(array, DYNAMIC))
        Force_Poison_Cell(Array_Tail(array));
  #endif

    Assert_Array(array);

    SERIES_INDEX_UNBOUNDED(list) = tail;
    return none;
}


// !!! This should probably chain together with Error_Bad_Utf8_Bin_Edit_Raw()
// to give some context for the error.  There are other examples of this
// error chaining that need to be hammered out.
//
static Error* Error_Bad_Utf8_Bin_Edit(Error* cause) {
    return cause;
}


//
//  Modify_String_Or_Blob: C
//
// This returns the index of the tail of the insertion.  The reason it does
// so is because the caller would have a hard time calculating that if the
// input Flex were FORM'd.
//
// It is possible to alias ANY-STRING? as BLOB! (or alias a binary as string,
// but doing so marks the Flex with FLEX_FLAG_IS_STRING).  If a Blob's Binary
// is aliased anywhere as a String Flex, it must carry this flag--and once it
// does so, then all mutations must preserve the Flex content as valid UTF-8.
// That aliasing ability is why this routine is for both string and binary.
//
// While a BLOB! and an ANY-STRING? can alias the same Flex, the meaning
// of Series_Index() is different.  So in addition to the detection of the
// FLEX_FLAG_IS_STRING on the Flex, we must know if dst is a BLOB!.
//
Result(None) Modify_String_Or_Blob(
    Element* series,  // ANY-STRING? or BLOB! value to modify
    ModifyState op,  // APPEND @ tail, INSERT or CHANGE @ index
    const Stable* v,  // argument with content to inject
    Flags flags,  // AM_LINE
    Length part,  // dst to remove (CHANGE) or limit to grow (APPEND or INSERT)
    Count dups  // dup count of how many times to insert the src content
){
    assert(
        op == ST_MODIFY_APPEND
        or op == ST_MODIFY_INSERT
        or op == ST_MODIFY_CHANGE
    );
    assert(dups > 0);  // use native entry points for "weird" cases [A]
    assert(part >= 0);
    assert(not Is_Antiform(v) or Is_Splice(v));

    Ensure_Mutable(series);  // note this also rules out ANY-WORD?s

    Binary* binary = cast(Binary*, Cell_Flex_Ensure_Mutable(series));
    assert(not Is_Stub_Symbol(binary));  // would be immutable

    Strand* strand = Is_Stub_Strand(binary)
        ? cast(Strand*, binary)
        : nullptr;

    Length index = Series_Index(series);
    Size used = Binary_Len(binary);

    REBLEN dst_len_old = 0xDECAFBAD;  // only if Is_Stub_Strand(binary)
    Size offset;
    if (Is_Blob(series)) {  // check invariants up front even if NULL / no-op
        if (strand) {
            Byte at = *Binary_At(strand, index);
            if (Is_Continuation_Byte(at))
                panic (Error_Bad_Utf8_Bin_Edit_Raw());
            dst_len_old = Strand_Len(strand);
        }
        offset = index;
    }
    else {
        assert(Any_String(series));

        offset = String_Byte_Offset_For_Index(series, index);  // !!! review speed
        dst_len_old = Strand_Len(strand);
    }

    // For INSERT:PART and APPEND:PART
    //
    Option(const Length*) limit;
    if (op != ST_MODIFY_CHANGE) {
        if (part <= 0) {
            SERIES_INDEX_UNBOUNDED(series) = op == ST_MODIFY_APPEND ? 0 : index;
            return none;
        }
        limit = &part;
    }
    else
        limit = UNLIMITED;

    // Now that we know there's actual work to do, we need `index` to speak
    // in terms of codepoints (if applicable)

    if (op == ST_MODIFY_APPEND or offset > used) {
        offset = Binary_Len(binary);
        index = dst_len_old;
    }
    else if (Is_Blob(series) and strand) {
        index = Strand_Index_At(strand, offset);
    }

    // If the src is not an ANY-STRING?, then we need to create string data
    // from the value to use its content.
    //
    DECLARE_MOLDER (mo);  // mo->strand will be non-null if Push_Mold() run

    const Byte* src;  // start of bytes (potentially UTF-8) to insert
    Length len;  // src length in codepoints (if dest is string)
    Size size;  // src size in bytes

    Byte src_byte;  // only used by BLOB! (mold buffer is UTF-8 legal)

    if (Is_Rune(v)) {  // characters store their encoding in their payload
        //
        // !!! We pass in UNLIMITED for the limit of how long the input is
        // because currently :PART speaks in terms of the destination series.
        // However, if that were changed to :LIMIT then we would want to
        // be cropping the :PART of the input via passing a parameter here.
        //
        src = Cell_Utf8_Len_Size_At_Limit(
            &len,
            &size,
            v,
            UNLIMITED
        );

        if (strand) {
            if (len == 0)
                panic (Error_Illegal_Zero_Byte_Raw());  // no '\0' in strings
        }
        else {
            if (len == 0)
                len = size = 1;  // Use the '\0' null term
            else
                len = size;  // binary counts length in bytes
        }
    }
    else if (
        Any_String(v)
        and not Is_Tag(v)  // tags need `<` and `>` to render
    ){
        // !!! Branch is very similar to the one for RUNE! above (merge?)

        // If Source == Destination we must prevent possible conflicts in
        // the memory regions being moved.  Clone the Flex just to be safe.
        //
        // !!! It may be possible to optimize special cases like append.
        //
        if (binary == Cell_Flex(v))
            goto form;

        src = String_At(v);

        // !!! We pass in UNLIMITED for the limit of how long the input is
        // because currently :PART speaks in terms of the destination series.
        // However, if that were changed to :LIMIT then we would want to
        // be cropping the :PART of the input via passing a parameter here.
        //
        size = String_Size_Limit_At(&len, v, UNLIMITED);
        if (not strand)
            len = size;
    }
    else if (Is_Integer(v)) {
        if (not Is_Blob(series))
            goto form;  // e.g. `append "abc" 10` is "abc10"

        // otherwise `append #{123456} 10` is #{1234560A}, just the byte

        src_byte = VAL_UINT8(v);  // panics if out of range
        if (strand and Is_Utf8_Lead_Byte(src_byte))
            panic (Error_Bad_Utf8_Bin_Edit_Raw());

        src = &src_byte;
        len = size = 1;
    }
    else if (Is_Blob(v)) {
        const Binary* other = Cell_Binary(v);

        src = Blob_Size_At(&size, v);

        if (not strand) {
            if (limit and *(unwrap limit) < size)
                size = *(unwrap limit);  // byte count for blob! dest
            len = size;
        }
        else {
            if (Is_Stub_Strand(other)) {  // guaranteed valid UTF-8
                const Strand* other_as_strand = cast(Strand*, other);
                if (Is_Continuation_Byte(*src))
                    panic (Error_Bad_Utf8_Bin_Edit_Raw());

                // !!! We could be more optimal here since we know it's valid
                // UTF-8 than walking characters up to the limit, like:
                //
                // `src_len_raw = Strand_Len(str) - Strand_Index_At(str, offset);`
                //
                // But for simplicity just use the same branch that unverified
                // binaries do for now.  This code can be optimized when the
                // functionality has been proven for a while.
                //
                UNUSED(other_as_strand);
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
                len = 0;

                Size bytes_left = size;
                const Byte* bp = src;
                for (; bytes_left > 0; --bytes_left, ++bp) {
                    Codepoint c = *bp;
                    if (Is_Byte_Ascii(c)) {  // just check for 0 bytes
                        if (c == '\0')
                            panic (Error_Bad_Utf8_Bin_Edit(
                                Error_Illegal_Zero_Byte_Raw()
                            ));
                    }
                    else {
                        c = Back_Scan_Utf8_Char(
                            &bp, &bytes_left
                        ) except (Error* e) {
                            panic (Error_Bad_Utf8_Bin_Edit(e));
                        }
                    }
                    ++len;

                    if (limit and *(unwrap limit) == len)
                        break;  // Note: :PART is count in codepoints
                }
            }
        }

        // We have to worry about conflicts and resizes if the source and
        // destination are the same.  Special cases like APPEND might be
        // optimizable here, but appending series to themselves is rare-ish.
        // Use the byte buffer.
        //
        if (other == binary) {
            Set_Flex_Len(BYTE_BUF, 0);
            trap (
              Expand_Flex_Tail_And_Update_Used(BYTE_BUF, size)
            );
            memcpy(Binary_Head(BYTE_BUF), src, size);
            src = Binary_Head(BYTE_BUF);
        }

        goto binary_limit_accounted_for;
    }
    else if (Is_Splice(v)) {
        //
        // !!! For APPEND and INSERT, the :PART should apply to *block* units,
        // and not character units from the generated string.

        if (Is_Blob(series)) {
            //
            // !!! R3-Alpha had the notion of joining a binary into a global
            // buffer that was cleared out and reused.  This was not geared
            // to be safe for threading.  It might be unified with the mold
            // buffer now that they are both byte-oriented...though there may
            // be some advantage to the mold buffer being UTF-8 only.
            //
            DECLARE_ELEMENT (group);
            Copy_Lifted_Cell(group, v);
            LIFT_BYTE(group) = NOQUOTE_2;
            Join_Binary_In_Byte_Buf(group, -1);
            src = Binary_Head(BYTE_BUF);  // cleared each time
            len = size = Binary_Len(BYTE_BUF);
        }
        else {
            Push_Mold(mo);

            // !!! The logic for APPEND or INSERT or CHAINGE on ANY-STRING? of
            // BLOCK! has been to form them without reducing, and no spaces
            // between.  There is some rationale to this, though implications
            // for operations like TO TEXT! of a BLOCK! are unclear...
            //
            const Element* item_tail;
            const Element* item = List_At(&item_tail, v);
            for (; item != item_tail; ++item)
                Form_Element(mo, item);
            goto use_mold_buffer;
        }
    }
    else { form:

        Push_Mold(mo);
        Mold_Or_Form_Element(mo, Known_Element(v), true);

        // Don't capture pointer until after mold (it may expand the buffer)

      use_mold_buffer:

        src = Binary_At(mo->strand, mo->base.size);
        size = Strand_Size(mo->strand) - mo->base.size;
        if (strand)
            len = Strand_Len(mo->strand) - mo->base.index;
        else
            len = size;
    }

    // Here we are accounting for a :PART where we know the source series
    // data is valid UTF-8.  (If the source were a BLOB!, where the :PART
    // counts in bytes, it would have jumped below here with limit set up.)
    //
    // !!! Bad first implementation; improve.
    //
    if (strand) {
        Utf8(const*) t = cast(Utf8(const*), src + size);
        if (limit)
            while (len > *(unwrap limit)) {
                t = Step_Back_Codepoint(t);
                --len;
            }
        size = t - src;  // len now equals limit
    }
    else {  // copying valid UTF-8 data possibly partially in bytes (!)
        if (limit and size > *(unwrap limit))
            size = *(unwrap limit);
        len = size;
    }

  binary_limit_accounted_for: ;  // needs ; (next line is declaration)

    Size expansion_size;  // includes duplicates and newlines, if applicable
    Length expansion_len;
    if (flags & AM_LINE) {
        expansion_size = (size + 1) * dups;
        expansion_len = (len + 1) * dups;
    }
    else {
        expansion_size = size * dups;
        expansion_len = len * dups;
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

    if (op == ST_MODIFY_APPEND or op == ST_MODIFY_INSERT) {  // always expands
        trap (
          Expand_Flex_At_Index_And_Update_Used(
            binary, offset, expansion_size
        ));

        if (strand) {
            book = opt Link_Bookmarks(strand);

            if (book and BOOKMARK_INDEX(book) > index) {  // only INSERT
                BOOKMARK_INDEX(book) += expansion_len;
                BOOKMARK_OFFSET(book) += expansion_size;
            }
            Tweak_Misc_Num_Codepoints(
                strand, dst_len_old + expansion_len
            );
        }
    }
    else {  // CHANGE only expands if more content added than overwritten
        assert(op == ST_MODIFY_CHANGE);

        REBLEN dst_len_at;
        Size dst_size_at;
        if (strand) {
            if (Is_Blob(series)) {
                dst_size_at = Series_Len_At(series);  // byte count
                dst_len_at = Strand_Index_At(strand, dst_size_at);
            }
            else
                dst_size_at = String_Size_Limit_At(
                    &dst_len_at,
                    series,
                    UNLIMITED
                );

            // Note: above functions may update the bookmarks --^
            //
            book = opt Link_Bookmarks(strand);
        }
        else {
            dst_len_at = Series_Len_At(series);
            dst_size_at = dst_len_at;
        }

        // We are overwriting codepoints where the source codepoint sizes and
        // the destination codepoint sizes may be different.  Hence if we
        // were changing a four-codepoint sequence where all are 1 byte with
        // a single-codepoint sequence with a 4-byte codepoint, you get:
        //
        //     len == 1
        //     dst_len_at == 4
        //     expansion_size == 4
        //     dst_size_at == 4
        //
        // It deceptively seems there's enough capacity.  But since only one
        // codepoint is being overwritten (with a larger one), three bytes
        // have to be moved safely out of the way before being overwritten.

        Size part_size;
        if (strand) {
            if (Is_Blob(series)) {
                //
                // The calculations on the new length depend on `part` being
                // in terms of codepoint count.  Transform it from byte count,
                // and also be sure it's a legitimate codepoint boundary and
                // not splitting a codepoint's bytes.
                //
                if (part > dst_size_at) {  // can use Strand_Len() from above
                    part = dst_len_at;
                    part_size = dst_size_at;
                }
                else {  // count how many codepoints are in the `part`
                    part_size = part;
                    Utf8(*) cp = cast(Utf8(*), Binary_At(binary, offset));
                    Utf8(*) pp = cast(Utf8(*),
                        Binary_At(binary, offset + part_size)
                    );
                    if (Is_Continuation_Byte(*cast(Byte*, pp)))
                        panic (Error_Bad_Utf8_Bin_Edit_Raw());

                    part = 0;
                    for (; cp != pp; cp = Skip_Codepoint(cp))
                        ++part;
                }
            }
            else {
                if (part > dst_len_at) {  // can use Strand_Len() from above
                    part = dst_len_at;
                    part_size = dst_size_at;
                }
                else {
                    REBLEN check;  // v-- !!! This call uses bookmark, review
                    part_size = String_Size_Limit_At(&check, series, &part);
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

        if (expansion_size > part_size) {
            //
            // We're adding more bytes than we're taking out.  Expand.
            //
            trap (
              Expand_Flex_At_Index_And_Update_Used(
                binary,
                offset,
                expansion_size - part_size
            ));
        }
        else if (part_size > expansion_size) {
            //
            // We're taking out more bytes than we're inserting.  Slide left.
            //
            Remove_Flex_Units_And_Update_Used(
                binary,
                offset,
                part_size - expansion_size
            );
        }
        else {
            // staying the same size (change "abc" "-" => "-bc")
        }

        // CHANGE can do arbitrary changes to what index maps to what offset
        // in the region of interest.  The manipulations here would be
        // complicated--but just assume that the start of the change is as
        // good a cache as any to be relevant for the next operation.
        //
        if (strand) {
            book = opt Link_Bookmarks(strand);

            if (book and BOOKMARK_INDEX(book) > index) {
                BOOKMARK_INDEX(book) = index;
                BOOKMARK_OFFSET(book) = offset;
            }
            Tweak_Misc_Num_Codepoints(
                strand, dst_len_old + expansion_len - part
            );
        }
    }

    // Since the Flex may be expanded, its pointer could change...so this
    // can't be done up front at the top of this routine.
    //
    Byte* dst = Binary_At(binary, offset);

    REBLEN d;
    for (d = 0; d < dups; ++d) {  // dups checked above as > 0
        memcpy(dst, src, size);
        dst += size;

        if (flags & AM_LINE) {  // line is not actually in inserted material
            *dst = '\n';
            ++dst;
        }
    }

    if (mo->strand != nullptr)  // ...a Push_Mold() happened
        Drop_Mold(mo);

    // !!! Should BYTE_BUF's memory be reclaimed also (or should it be
    // unified with the mold buffer?)

    if (book) {
        if (BOOKMARK_INDEX(book) > Strand_Len(strand)) {  // past active
            assert(op == ST_MODIFY_CHANGE);  // only change removes material
            Free_Bookmarks_Maybe_Null(strand);
        }
        else {
          #if DEBUG_BOOKMARKS_ON_MODIFY
            Check_Bookmarks_Debug(strand);
          #endif

            if (Strand_Len(strand) < Size_Of(Cell))  // small not kept
                Free_Bookmarks_Maybe_Null(strand);
        }
    }

    // !!! Set_Flex_Used() now corrupts the terminating byte, which notices
    // problems when it's not synchronized.  Review why the above code does
    // not always produce a legitimate termination.
    //
    Term_Flex_If_Necessary(binary);

    if (op == ST_MODIFY_APPEND) {
        SERIES_INDEX_UNBOUNDED(series) = 0;
        return none;
    }

    if (Is_Blob(series)) {
        SERIES_INDEX_UNBOUNDED(series) = offset + expansion_size;
        return none;
    }

    SERIES_INDEX_UNBOUNDED(series) = index + expansion_len;
    return none;
}
