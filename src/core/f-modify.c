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

    Option(const Length*) limit;  // how much of `v` to inject (if splice)
    if (op == ST_MODIFY_CHANGE)
        limit = UNLIMITED;  // always uses all of `v`
    else
        limit = &part;  // may limit how much of `v` is used

  setup_destination: {

    Source* array = Cell_Array_Ensure_Mutable(list);
    REBLEN index = SERIES_INDEX_UNBOUNDED(list);  // !!! bounded?
    REBLEN tail = Array_Len(array);

    if (op == ST_MODIFY_APPEND or index > tail)
        index = tail;

  setup_newlines: {

  // Each dup being inserted need a newline signal after it if:
  //
  // * The user explicitly invokes the :LINE refinement (AM_LINE flag)
  //
  // * It's a spliced insertion and a NEWLINE_BEFORE flag is on the element
  //   element *after* the last item in the dup
  //
  // * It's a spliced insertion and there dup goes to the end of the array
  //   so there's no element after the last item, but NEWLINE_AT_TAIL is set
  //   on the inserted array.
  //
  // 1. Beyond newlines on the cells being inserted, there is also the chance
  //    there was a newline tail flag on the target array, and the insertion
  //    is at the end...so that flag may need to proxy on an inserted cell.

    bool tail_newline = did (flags & AM_LINE);

    bool head_newline =
        (index == Array_Len(array))
        and Get_Source_Flag(array, NEWLINE_AT_TAIL);

  setup_source: {

  // 1. Self-splicing isn't very common, but we don't want to crash due to
  //    the memory overlap.  Because it's rare this creates a managed series
  //    and lets the GC free it, but really we could not manage the array
  //    and free it in this routine--review.

    const Element* src;
    Length splice_len;

    if (Is_Splice(v)) {
        Length len_at = Series_Len_At(v);
        splice_len = len_at;

        if (limit and *(unwrap limit) < splice_len)
            splice_len = *(unwrap limit);

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

        if (array == Cell_Array(v)) {  // !!! temp array for self-splice [1]
            Array* copy = Copy_Array_At_Extra_Shallow(
                STUB_MASK_MANAGED_SOURCE,
                Cell_Array(v),
                Series_Index(v),
                0 // extra
            );
            src = Array_Head(copy);
        }
        else
            src = List_At(nullptr, v);  // may be tail
    }
    else {  // use passed in Cell
        splice_len = 1;
        src = Known_Element(v);
    }

    Length expansion = dups * splice_len;  // total to insert (dups is > 0)

  expand_or_resize_array: {

    if (op != ST_MODIFY_CHANGE) {  // Always expand for INSERT and APPEND
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

} perform_insertions: {

  // 1. We wait to clear the NEWLINE_AT_TAIL flag on the target array until
  //    the loop actually makes a value that can take over encoding the bit.

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
                Clear_Source_Flag(array, NEWLINE_AT_TAIL);  // [1]
                continue;
            }

            if (dup_index > 0 and i == 0 and tail_newline) {
                Set_Cell_Flag(Array_Head(array) + index, NEWLINE_BEFORE);
            }
        }
    }

} finalize_newlines: {

  // The insert loop only puts on (dups - 1) NEWLINE_BEFORE flags.  The last
  // one might have to use the array flag.  See SOURCE_FLAG_NEWLINE_AT_TAIL.
  //
  // 1. Heuristic: if a line is added to the list with the explicit :LINE
  //    flag, force the head element to have a newline.  Remove if you want:
  //
  //        >> x: copy []
  //        >> append:line x [a b c]
  //        == [
  //            [a b c]
  //        ]
  //

    if (tail_newline) {
        if (index == Array_Len(array))
            Set_Source_Flag(array, NEWLINE_AT_TAIL);
        else
            Set_Cell_Flag(Array_At(array, index), NEWLINE_BEFORE);
    }

    if (flags & AM_LINE)  // !!! testing this heuristic [1]
        Set_Cell_Flag(Array_Head(array), NEWLINE_BEFORE);

} finish_up: {

  #if DEBUG_POISON_FLEX_TAILS
    if (Get_Stub_Flag(array, DYNAMIC))
        Force_Poison_Cell(Array_Tail(array));
  #endif

    Assert_Array(array);

    SERIES_INDEX_UNBOUNDED(list) = tail;
    return none;
}}}}}


// !!! This should probably chain together with Error_Bad_Utf8_Bin_Edit_Raw()
// to give some context for the error.  There are other examples of this
// error chaining that need to be hammered out.
//
static Error* Error_Bad_Utf8_Bin_Edit(Error* cause) {
    return cause;
}


//
//  Join_Binary_In_Byte_Buf: C
//
// !!! This routine uses a different buffer from molding, because molding
// currently has to maintain valid UTF-8 data.  It may be that the buffers
// should be unified.
//
static void Join_Binary_In_Byte_Buf(
    const Stable* splice,
    Option(const Length*) limit
){
    assert(Is_Splice(splice));

    Binary* buf = BYTE_BUF;

    REBLEN tail = 0;

    Length count = limit ? *(unwrap limit) : Series_Len_At(splice);

    Set_Flex_Len(buf, 0);

    const Element* val_tail;
    const Element* val = List_At(&val_tail, splice);
    for (; count > 0 and val != val_tail; ++val, --count) {
        switch (opt Type_Of(val)) {
          case TYPE_QUASIFORM:
            panic (Error_Bad_Value(val));

          case TYPE_INTEGER: {
            require (
              Expand_Flex_Tail_And_Update_Used(buf, 1)
            );
            *Binary_At(buf, tail) = cast(Byte, VAL_UINT8(val));  // can panic()
            break; }

          case TYPE_BLOB: {
            Size size;
            const Byte* data = Blob_Size_At(&size, val);
            require (
              Expand_Flex_Tail_And_Update_Used(buf, size)
            );
            memcpy(Binary_At(buf, tail), data, size);
            break; }

          case TYPE_RUNE:
          case TYPE_TEXT:
          case TYPE_FILE:
          case TYPE_EMAIL:
          case TYPE_URL:
          case TYPE_TAG: {
            Size utf8_size;
            Utf8(const*) utf8 = Cell_Utf8_Size_At(&utf8_size, val);

            require (
              Expand_Flex_Tail_And_Update_Used(buf, utf8_size)
            );
            memcpy(Binary_At(buf, tail), cast(Byte*, utf8), utf8_size);
            Set_Flex_Len(buf, tail + utf8_size);
            break; }

          default:
            panic (Error_Bad_Value(val));
        }

        tail = Flex_Used(buf);
    }

    *Binary_At(buf, tail) = 0;
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

    Option(const Length*) limit;  // how much of `v` to inject
    if (op == ST_MODIFY_CHANGE)
        limit = UNLIMITED;  // always uses all of `v`
    else
        limit = &part;  // may limit how much of `v` is used

  setup_destination: {

  // The `binary` is the Flex being modified.  It can be either just a Binary*
  // or it can be a Strand* if the Binary is actually a string alias.
  //
  // 1. Rather than testing for Is_Stub_Strand() and then casting multiple
  //    times in the code below, we keep a `strand` variable that is either
  //    null (if not a string) or the Strand* version of the binary if it is.

    Binary* binary = cast(Binary*, Cell_Flex_Ensure_Mutable(series));

    Strand* strand = Is_Stub_Strand(binary)
        ? cast(Strand*, binary)
        : nullptr;  // alias for binary, null if not a Strand [1]

    Size used = Binary_Len(binary);

    Length dst_len_old_store;
    Length* dst_len_old = (strand ? &dst_len_old_store : nullptr);

    Size offset;

    Length index_store;
    Length* index;

    if (Is_Blob(series)) {  // check invariants up front even if NULL / no-op
        index = nullptr;
        offset = Series_Index(series);
        if (strand) {
            Byte at = *Binary_At(strand, offset);
            if (Is_Continuation_Byte(at))
                panic (Error_Bad_Utf8_Bin_Edit_Raw());
            *dst_len_old = Strand_Len(strand);
        }
    }
    else {
        assert(Any_String(series));

        index = &index_store;
        *index = Series_Index(series);
        offset = String_Byte_Offset_For_Index(series, *index);  // !!! speedup?
        *dst_len_old = Strand_Len(strand);
    }

    // Now that we know there's actual work to do, we need `index` to speak
    // in terms of codepoints (if applicable)

    if (op == ST_MODIFY_APPEND or offset > used) {
        offset = Binary_Len(binary);
        if (strand)
            *index = *dst_len_old;
    }
    else if (Is_Blob(series) and strand) {
        *index = Strand_Index_At(strand, offset);
    }

  setup_source: {

  // We calculate `src`, and `size` in bytes for data (`v`) we are inserting.
  //
  // 1. If the target `series` is a UTF-8 Strand, then we have to know not
  //    just the bytes and size we are inserting, but that what we are
  //    inserting is valid UTF-8 *and* know its length in codepoints too.
  //
  // 2. If `v` is not naturally a source of bytes (like a string or binary)
  //    then we may have to mold it into a UTF-8 representation.  Also, if
  //    `v` aliases `series` we may have to copy the data into the mold
  //    buffer to avoid overlap.
  //
  //    mo->strand will be non-null if Push_Mold() runs

    const Byte* src;
    Size size;

    Length len_store;
    Length* len;  // codepoint count needed if targeting a Strand [1]
    len = (strand ? &len_store : nullptr);

    DECLARE_MOLDER (mo);  // src may be set to point into mold buffer [2]

  dispatch_on_type: {

    if (Any_Utf8(v))
        goto handle_utf8;

    if (Is_Integer(v))
        goto handle_integer;

    if (Is_Blob(v))
        goto handle_blob;

    if (Is_Splice(v))
        goto handle_splice;

    goto handle_generic_form;

} handle_utf8: { /////////////////////////////////////////////////////////////

  // 1. We have to worry about conflicts and resizes if source and destination
  //    are the same.  Special cases like APPEND might be optimizable here,
  //    but appending series to themselves is rare-ish.  Use the mold buffer.

    Length utf8_len;
    src = Cell_Utf8_Len_Size_At_Limit(
        &utf8_len,  // calculate regardless in case needed for [1]
        &size,
        v,
        limit
    );

    if (strand)
        *len = utf8_len;

    if (
        Stringlike_Has_Stub(v)
        and Cell_Flex(v) == binary  // conservative, copy to mold buffer [1]
    ){
        Push_Mold(mo);
        Append_Utf8(mo->strand, src, utf8_len, size);
        src = Binary_At(mo->strand, mo->base.size);
        goto use_mold_buffer;
    }

    goto src_and_len_and_size_known;

} handle_integer: { //////////////////////////////////////////////////////////

  // Note that (append #{123456} 10) is #{1234560A}, just the byte.
  // But (append "abc" 10) is "abc10"

    if (not Is_Blob(series))
        goto handle_generic_form;  // don't want single byte interpretation

    Byte byte = VAL_UINT8(v);  // panics if out of range

    if (strand) {
        if (Is_Utf8_Lead_Byte(byte))
            panic (Error_Bad_Utf8_Bin_Edit_Raw());
        if (byte == '\0')
            panic (Error_Illegal_Zero_Byte_Raw());

        *len = 1;
    }

    size = 1;

    Set_Flex_Len(BYTE_BUF, 0);
    trap (
        Expand_Flex_Tail_And_Update_Used(BYTE_BUF, size)
    );
    *Binary_Head(BYTE_BUF) = byte;
    src = Binary_Head(BYTE_BUF);

    goto src_and_len_and_size_known;

} handle_blob: { /////////////////////////////////////////////////////////////

  // 1. We could be more optimal here since we know it's valid UTF-8 than
  //    walking characters up to the limit.  But for simplicity we use the
  //    same branch as unverified UTF-8 blobs for now.
  //
  // 2. The binary may be invalid UTF-8.  We don't need to worry about the
  //    *entire* binary, just the part we are adding (whereas AS has to worry
  //    for aliasing, since BACK and HEAD are still possible)
  //
  // 3. We have to worry about conflicts and resizes if source and destination
  //    are the same.  Special cases like APPEND might be optimizable here,
  //    but appending series to themselves is rare-ish.  Use the byte buffer.

    const Binary* other = Cell_Binary(v);

    src = Blob_Size_At(&size, v);

    if (not strand) {
        if (limit and *(unwrap limit) < size)
            size = *(unwrap limit);  // byte count for blob! dest
    }
    else if (Is_Stub_Strand(other)) {  // guaranteed valid UTF-8
        const Strand* other_as_strand = cast(Strand*, other);
        if (Is_Continuation_Byte(*src))
            panic (Error_Bad_Utf8_Bin_Edit_Raw());

        UNUSED(other_as_strand);  // we don't exploit UTF-8 validity yet [1]
        goto unverified_utf8_src_binary;
    }
    else {
       unverified_utf8_src_binary:  // only needs to be valid up to :PART [2]
        *len = 0;

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
            ++(*len);

            if (limit and *(unwrap limit) == *len)
                break;  // Note: :PART is count in codepoints
        }
    }

    if (other == binary) {  // make copy in BYTE_BUF to avoid overlap [3]
        Set_Flex_Len(BYTE_BUF, 0);
        trap (
            Expand_Flex_Tail_And_Update_Used(BYTE_BUF, size)
        );
        memcpy(Binary_Head(BYTE_BUF), src, size);
        src = Binary_Head(BYTE_BUF);
    }

    goto src_and_len_and_size_known;

} handle_splice: { ///////////////////////////////////////////////////////////

  // 1. !!! R3-Alpha had the notion of joining a binary into a global buffer
  //    that was cleared out and reused.  This was not geared to be safe for
  //    threading.  It might be unified with the mold buffer now that they are
  //    both byte-oriented...though there may be some advantage to the mold
  //    buffer being UTF-8 only.
  //
  // 2. !!! The logic for APPEND or INSERT or CHANGE on ANY-STRING? of BLOCK!
  //    historically was to form elements without reducing, and no spacing.

    if (not strand) {  // join in BYTE_BUF, R3-Alpha idea [1]
        Join_Binary_In_Byte_Buf(v, limit);
        src = Binary_Head(BYTE_BUF);  // cleared each time
        size = Binary_Len(BYTE_BUF);
    }
    else {  // form individual elements into mold buffer, no spacing [2]
        Push_Mold(mo);

        Length count = limit ? *(unwrap limit) : Series_Len_At(v);

        const Element* item_tail;
        const Element* item = List_At(&item_tail, v);
        for (; count != 0 and item != item_tail; --count, ++item)
            Form_Element(mo, item);

        goto use_mold_buffer;  // assigns [src size len]
    }

    goto src_and_len_and_size_known;

} handle_generic_form: {  ////////////////////////////////////////////////////

    Push_Mold(mo);
    Mold_Or_Form_Element(mo, Known_Element(v), true);

    // Don't capture `src` pointer until after mold (it may expand the buffer)

    goto use_mold_buffer;

} use_mold_buffer: { /////////////////////////////////////////////////////////

    src = Binary_At(mo->strand, mo->base.size);
    size = Strand_Size(mo->strand) - mo->base.size;
    if (strand)
        *len = Strand_Len(mo->strand) - mo->base.index;

    goto src_and_len_and_size_known;

} src_and_len_and_size_known: { //////////////////////////////////////////////

    Size expansion_size;  // includes duplicates and newlines, if applicable

    Length expansion_len_store;
    Length* expansion_len;
    expansion_len = (strand ? &expansion_len_store : nullptr);

    if (flags & AM_LINE) {
        expansion_size = (size + 1) * dups;
        if (strand)
            *expansion_len = (*len + 1) * dups;
    }
    else {
        expansion_size = size * dups;
        if (strand)
            *expansion_len = (*len) * dups;
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

            if (book and BOOKMARK_INDEX(book) > *index) {  // only INSERT
                BOOKMARK_INDEX(book) += *expansion_len;
                BOOKMARK_OFFSET(book) += expansion_size;
            }
            Tweak_Misc_Num_Codepoints(
                strand, *dst_len_old + *expansion_len
            );
        }
    }
    else {  // CHANGE only expands if more content added than overwritten
        assert(op == ST_MODIFY_CHANGE);

        Size dst_size_at;

        Length dst_len_at_store;
        Length* dst_len_at;
        dst_len_at = (strand ? &dst_len_at_store : nullptr);

        if (strand) {
            if (Is_Blob(series)) {
                dst_size_at = Series_Len_At(series);  // byte count
                *dst_len_at = Strand_Index_At(strand, dst_size_at);
            }
            else
                dst_size_at = String_Size_Limit_At(
                    &*dst_len_at,
                    series,
                    UNLIMITED
                );

            // Note: above functions may update the bookmarks --^
            //
            book = opt Link_Bookmarks(strand);
        }
        else {
            dst_size_at = Series_Len_At(series);
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
                    part = *dst_len_at;
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
                if (part > *dst_len_at) {  // can use Strand_Len() from above
                    part = *dst_len_at;
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

            if (book and BOOKMARK_INDEX(book) > *index) {
                BOOKMARK_INDEX(book) = *index;
                BOOKMARK_OFFSET(book) = offset;
            }
            Tweak_Misc_Num_Codepoints(
                strand, *dst_len_old + *expansion_len - part
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

    SERIES_INDEX_UNBOUNDED(series) = *index + *expansion_len;
    return none;
}}}}
