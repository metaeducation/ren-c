//
//  file: %t-binary.c
//  summary: "BLOB! datatype"
//  section: datatypes
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
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
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include "sys-core.h"
#include "sys-int-funcs.h"

#undef Byte  // sys-zlib.h defines it compatibly (unsigned char)
#include "sys-zlib.h"  // for crc32_z()


//
//  CT_Blob: C
//
REBINT CT_Blob(const Element* a, const Element* b, bool strict)
{
    UNUSED(strict);  // no lax form of comparison

    Size size1;
    const Byte* data1 = Blob_Size_At(&size1, a);

    Size size2;
    const Byte* data2 = Blob_Size_At(&size2, b);

    Size size = MIN(size1, size2);

    REBINT n = memcmp(data1, data2, size);

    if (n != 0)  // not guaranteed to be strictly in [-1 0 1]
        return n > 0 ? 1 : -1;

    if (size1 == size2)
        return 0;

    return size1 > size2 ? 1 : -1;
}


IMPLEMENT_GENERIC(EQUAL_Q, Is_Blob)
{
    INCLUDE_PARAMS_OF_EQUAL_Q;
    bool strict = not ARG(RELAX);

    Element* v1 = Element_ARG(VALUE1);
    Element* v2 = Element_ARG(VALUE2);

    return LOGIC(CT_Blob(v1, v2, strict) == 0);
}


IMPLEMENT_GENERIC(LESSER_Q, Is_Blob)
{
    INCLUDE_PARAMS_OF_LESSER_Q;

    Element* v1 = Element_ARG(VALUE1);
    Element* v2 = Element_ARG(VALUE2);

    return LOGIC(CT_Blob(v1, v2, true) == -1);
}



/***********************************************************************
**
**  Local Utility Functions
**
***********************************************************************/



//
//  encode-IEEE-754: native [
//      "Encode a decimal as binary blob according to the IEEE-754 standard"
//
//      return: [
//          blob! "Default return is double format (64 bits, 53-bit precision)"
//      ]
//      arg [decimal!]  ; REVIEW: ~NaN~, ~inf~ as antiforms
//      options "[single] -> Use single format (32 bits, 24-bit precision)"
//          [block!]
//  ]
//
DECLARE_NATIVE(ENCODE_IEEE_754) {
    INCLUDE_PARAMS_OF_ENCODE_IEEE_754;

    Stable* arg = ARG(ARG);

    if (Series_Len_At(ARG(OPTIONS)))
        panic ("IEEE-754 single precision not currently supported");

    assert(sizeof(REBDEC) == 8);

    Binary* bin = Make_Binary(8);
    Byte* bp = Binary_Head(bin);

    REBDEC d = VAL_DECIMAL(arg);
    const Byte* cp = cast(Byte*, &d);

  #if defined(ENDIAN_LITTLE)
    REBLEN n;
    for (n = 0; n < 8; ++n)
        bp[n] = cp[7 - n];
  #elif defined(ENDIAN_BIG)
    REBLEN n;
    for (n = 0; n < 8; ++n)
        bp[n] = cp[n];
  #else
    #error "Unsupported CPU endian"
  #endif

    Term_Binary_Len(bin, 8);
    return Init_Blob(OUT, bin);
}


//
//  decode-IEEE-754: native [
//      "Decode binary blob as decimal according to the IEEE-754 standard"
//
//      return: [decimal!]  ; review ~NaN~, ~inf~ as antiforms
//      blob [blob!]
//      options "[single] -> Use single format (32 bits, 24-bit precision)"
//          [block!]
//  ]
//
DECLARE_NATIVE(DECODE_IEEE_754)
{
    INCLUDE_PARAMS_OF_DECODE_IEEE_754;

    Element* blob = Element_ARG(BLOB);

    if (Series_Len_At(ARG(OPTIONS)))
        panic ("IEEE-754 single precision not currently supported");

    Size size;
    const Byte* at = Blob_Size_At(&size, blob);
    if (size < 8)
        return fail (blob);

    Init(Element) out = OUT;
    Reset_Cell_Header_Noquote(TRACK(out), CELL_MASK_DECIMAL);

    Byte* dp = cast(Byte*, &VAL_DECIMAL(out));

  #if defined(ENDIAN_LITTLE)
    REBLEN n;
    for (n = 0; n < 8; ++n)
        dp[n] = at[7 - n];
  #elif defined(ENDIAN_BIG)
    REBLEN n;
    for (n = 0; n < 8; ++n)
        dp[n] = at[n];
  #else
    #error "Unsupported CPU endian"
  #endif

    return OUT;
}


// See also: IMPLEMENT_GENERIC(MAKE, Any_String), which is similar.
//
IMPLEMENT_GENERIC(MAKE, Is_Blob)
{
    INCLUDE_PARAMS_OF_MAKE;

    assert(Datatype_Type(ARG(TYPE)) == TYPE_BLOB);
    UNUSED(ARG(TYPE));

    Element* arg = Element_ARG(DEF);

    switch (opt Type_Of(arg)) {
      case TYPE_INTEGER:  // !!! R3-Alpha nebulously tolerated DECIMAL! :-(
        return Init_Blob(OUT, Make_Binary(Int32s(arg, 0)));

      case TYPE_TUPLE: {
        REBLEN len = Sequence_Len(arg);
        Binary* b = Make_Binary(len);
        Byte* head = Binary_Head(b);
        if (Try_Get_Sequence_Bytes(head, cast(Element*, arg), len)) {
            Term_Binary_Len(b, len);
            return Init_Blob(OUT, b);
        }
        panic (
            "TUPLE! did not consist entirely of INTEGER! values 0-255"
        ); }

      case TYPE_BITSET:
        return Init_Blob(
            OUT,
            Make_Binary_From_Sized_Bytes(
                Binary_Head(Cell_Binary(arg)),
                Series_Len_Head(arg)
            )
        );

      default:
        break;
    }

    return fail (Error_Bad_Make(TYPE_BLOB, arg));
}


// 1. Historial Rebol let you set your binary base molding in a global way.
//    If this is to be a console setting, that's one thing...but having a
//    flag like this changing the fundamental behavior is bad.  In addition
//    to the general variability of how a program would run, it was using
//    a setting in the system object...which is not avaliable in early boot.
//
IMPLEMENT_GENERIC(MOLDIFY, Is_Blob)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* v = Element_ARG(VALUE);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));
    bool form = did ARG(FORM);

    UNUSED(form);

    Size size;
    const Byte* data = Blob_Size_At(&size, v);

    if (GET_MOLD_FLAG(mo, MOLD_FLAG_LIMIT)) {  // truncation is imprecise...
        Length mold_len = Strand_Len(mo->strand) - mo->base.index;
        if (mold_len + (2 * size) > mo->limit) {  //
            size = (mo->limit - mold_len) / 2;
            SET_MOLD_FLAG(mo, MOLD_FLAG_WAS_TRUNCATED);
        }
    }

    REBINT binary_base = 16;  // molding based on system preference is bad [1]
    /* binary_base = Get_System_Int(SYS_OPTIONS, OPTIONS_BINARY_BASE, 16); */

    switch (binary_base) {
      default:
      case 16: {
        require (
          Append_Ascii(mo->strand, "#{") // #{...}, not #16{...}
        );
        const bool brk = (size > 32);
        Form_Base16(mo, data, size, brk);
        break; }

      case 64: {
        require (
          Append_Ascii(mo->strand, "64#{")
        );
        const bool brk = (size > 64);
        Form_Base64(mo, data, size, brk);
        break; }

      case 2: {
        require (
          Append_Ascii(mo->strand, "2#{")
        );
        const bool brk = (size > 8);
        Form_Base2(mo, data, size, brk);
        break; }
    }

    if (NOT_MOLD_FLAG(mo, MOLD_FLAG_WAS_TRUNCATED))
        Append_Codepoint(mo->strand, '}');

    return TRASH;
}


static Result(Element*) Copy_Blob_Part_At_May_Modify_Index(
    Sink(Element) out,
    Element* blob,  // may modify index
    Option(const Stable*) part
){
    Length len = Part_Len_May_Modify_Index(blob, part);
    trap (
      Binary* copy = Copy_Binary_At_Len(
        Cell_Binary(blob), Series_Index(blob), len
    ));
    return Init_Series(out, TYPE_BLOB, copy);
}


IMPLEMENT_GENERIC(OLDGENERIC, Is_Blob)
{
    const Symbol* verb = Level_Verb(LEVEL);
    Option(SymId) id = Symbol_Id(verb);

    Element* v = cast(Element*, ARG_N(1));
    assert(Is_Blob(v));

    switch (opt id) {
    //-- Modification:
      case SYM_APPEND:
      case SYM_INSERT:
      case SYM_CHANGE: {
        INCLUDE_PARAMS_OF_INSERT;  // compatible frame with APPEND, CHANGE
        UNUSED(PARAM(SERIES));  // covered by `v`

        Option(const Stable*) arg = ARG(VALUE);

        REBLEN len; // length of target
        if (id == SYM_CHANGE)
            len = Part_Len_May_Modify_Index(v, ARG(PART));
        else
            len = Part_Limit_Append_Insert(ARG(PART));

        // Note that while inserting or appending VOID is a no-op, CHANGE with
        // a :PART can actually erase data.
        //
        if (not arg and len == 0) {
            if (id == SYM_APPEND) // append always returns head
                SERIES_INDEX_UNBOUNDED(v) = 0;
            return COPY(v);  // don't panic on read only if would be a no-op
        }

        Flags flags = 0;
        if (ARG(PART))
            flags |= AM_PART;
        if (ARG(LINE))
            flags |= AM_LINE;

        // !!! This mimics the historical behavior for now:
        //
        //     rebol2>> append "abc" 'd
        //     == "abcd"
        //
        //     rebol2>> append/only "abc" [d e]  ; like appending (the '[d e])
        //     == "abcde"
        //
        // But for consistency, it would seem that if the incoming value is
        // quoted that should give molding semantics, so quoted blocks include
        // their brackets.  Review.
        //
        if (not arg) {
            // not necessarily a no-op (e.g. CHANGE can erase)
        }
        else if (Is_Antiform(unwrap arg))
        {
            assert(Is_Splice(unwrap arg));  // typecheck shouldn't pass others
        }

        require (
          SERIES_INDEX_UNBOUNDED(v) = Modify_String_Or_Blob(
            v,
            unwrap id,
            arg,
            flags,
            len,
            ARG(DUP) ? Int32(unwrap ARG(DUP)) : 1
        ));
        return COPY(v); }

    //-- Search:
      case SYM_SELECT:
      case SYM_FIND: {
        INCLUDE_PARAMS_OF_FIND;
        UNUSED(PARAM(SERIES));  // covered by `v`

        if (Is_Antiform(ARG(PATTERN)))
            panic (ARG(PATTERN));

        const Element* pattern = Element_ARG(PATTERN);

        Flags flags = (
            (ARG(MATCH) ? AM_FIND_MATCH : 0)
            | (ARG(CASE) ? AM_FIND_CASE : 0)
        );

        REBINT tail = Part_Tail_May_Modify_Index(v, ARG(PART));

        REBINT skip;
        if (ARG(SKIP))
            skip = VAL_INT32(unwrap ARG(SKIP));
        else
            skip = 1;

        REBLEN size;
        REBLEN ret = Find_Value_In_Binstr(  // returned length is byte index
            &size, v, tail, pattern, flags, skip
        );

        if (ret == NOT_FOUND)
            return NULLED;

        if (id == SYM_FIND) {
            Source* pack = Make_Source_Managed(2);
            Set_Flex_Len(pack, 2);

            Copy_Lifted_Cell(Array_At(pack, 0), v);
            SERIES_INDEX_UNBOUNDED(Array_At(pack, 0)) = ret;

            Copy_Lifted_Cell(Array_At(pack, 1), v);
            SERIES_INDEX_UNBOUNDED(Array_At(pack, 1)) = ret + size;

            return Init_Pack(OUT, pack);
        }
        else
            assert(id == SYM_SELECT);

        ret++;
        if (ret >= tail)
            return NULLED;

        return Init_Integer(OUT, *Binary_At(Cell_Binary(v), ret)); }

      case SYM_CLEAR: {
        Binary* b = Cell_Binary_Ensure_Mutable(v);

        REBINT tail = Series_Len_Head(v);
        REBINT index = Series_Index(v);

        if (index >= tail)
            return COPY(v); // clearing after available data has no effect

        // !!! R3-Alpha would take this opportunity to make it so that if the
        // series is now empty, it reclaims the "bias" (unused capacity at
        // the head of the Flex).  One of many behaviors worth reviewing.
        //
        if (index == 0 and Get_Stub_Flag(b, DYNAMIC))
            Unbias_Flex(b, false);

        Term_Binary_Len(b, index);  // may have string alias
        return COPY(v); }

    //-- Bitwise:

      case SYM_BITWISE_AND:
      case SYM_BITWISE_OR:
      case SYM_BITWISE_XOR:
      case SYM_BITWISE_AND_NOT: {
        Stable* arg = ARG_N(2);
        if (not Is_Blob(arg))
            panic (Error_Not_Related_Raw(verb, Datatype_Of(arg)));

        Size t0;
        const Byte* p0 = Blob_Size_At(&t0, v);

        Size t1;
        const Byte* p1 = Blob_Size_At(&t1, arg);

        Size smaller = MIN(t0, t1);  // smaller array size
        Size larger = MAX(t0, t1);

        Binary* b = Make_Binary(larger);
        Term_Binary_Len(b, larger);

        Byte* dest = Binary_Head(b);

        switch (opt id) {
          case SYM_BITWISE_AND: {
            REBLEN i;
            for (i = 0; i < smaller; i++)
                *dest++ = *p0++ & *p1++;
            memset(dest, 0, larger - smaller);
            break; }

          case SYM_BITWISE_OR: {
            REBLEN i;
            for (i = 0; i < smaller; i++)
                *dest++ = *p0++ | *p1++;
            memcpy(dest, ((t0 > t1) ? p0 : p1), larger - smaller);
            break; }

          case SYM_BITWISE_XOR: {
            REBLEN i;
            for (i = 0; i < smaller; i++)
                *dest++ = *p0++ ^ *p1++;
            memcpy(dest, ((t0 > t1) ? p0 : p1), larger - smaller);
            break; }

          case SYM_BITWISE_AND_NOT: {
            REBLEN i;
            for (i = 0; i < smaller; i++)
                *dest++ = *p0++ & ~*p1++;
            if (t0 > t1)
                memcpy(dest, p0, t0 - t1);
            break; }

          default:
            assert(false);  // not reachable
        }

        return Init_Blob(OUT, b); }

      case SYM_BITWISE_NOT: {
        Size size;
        const Byte* bp = Blob_Size_At(&size, v);

        Binary* bin = Make_Binary(size);
        Term_Binary_Len(bin, size);  // !!! size is decremented, must set now

        Byte* dp = Binary_Head(bin);
        for (; size > 0; --size, ++bp, ++dp)
            *dp = ~(*bp);

        return Init_Series(OUT, TYPE_BLOB, bin); }

    //-- Special actions:

      case SYM_SWAP: {
        Stable* arg = ARG_N(2);

        if (Type_Of(v) != Type_Of(arg))
            panic (Error_Not_Same_Type_Raw());

        Byte* v_at = Blob_At_Ensure_Mutable(v);
        Byte* arg_at = Blob_At_Ensure_Mutable(arg);

        REBINT tail = Series_Len_Head(v);
        REBINT index = Series_Index(v);

        if (index < tail and Series_Index(arg) < Series_Len_Head(arg)) {
            Byte temp = *v_at;
            *v_at = *arg_at;
            *arg_at = temp;
        }
        return COPY(v); }

      default:
        break;
    }

    panic (UNHANDLED);
}


// 1. !!! Historically TO would convert binaries to strings.  But as the
//    definition of TO has been questioned and evolving, that no longer
//    seems to make sense (e.g. if `TO TEXT! 1` is "1", the concept of
//    implementation transformations doesn't fit).  Keep compatible for
//    right now, but ultimately MAKE or AS should be used for this.
//
IMPLEMENT_GENERIC(TO, Is_Blob)
{
    INCLUDE_PARAMS_OF_TO;

    Element* v = Element_ARG(VALUE);
    Heart to = Datatype_Builtin_Heart(ARG(TYPE));

    if (Any_String_Type(to)) {  // (to text! binary) questionable [1]
        Size size;
        const Byte* at = Blob_Size_At(&size, v);
        return Init_Any_String(
            OUT,
            to,
            Append_UTF8_May_Panic(nullptr, s_cast(at), size, STRMODE_NO_CR)
        );
    }

    if (to == TYPE_BLOB) {
        const Stable* part = LIB(NULL);  // no :PART, copy to end
        require (
          Copy_Blob_Part_At_May_Modify_Index(OUT, v, part)
        );
        return OUT;
    }

    panic (UNHANDLED);
}

//
//   Alias_Blob_As: C
//
// The key aliasing AS conversion for binary BLOB!s is as UTF-8 data.
// It's a fair bit of effort, but can potentially save significantly
// on memory with things like `as text! read %some-file.txt` using no
// additional memory when that file is large.
//
// 1. We first alias the BLOB's Binary data as a string (if possible).
//    Then if further conversion is needed to an ANY-WORD? or non-string
//    UTF-8 type (like RUNE! or URL!), that subdispatches to the code
//    that converts strings.
//
// 2. There's no way to pass AS options for narrowing the validation of the
//    UTF-8 (e.g. no emoji or non-printable characters).  And DECODE 'UTF-8
//    can have those options, but it copies the data instead of aliasing it.
//    This suggests a need for some factoring of validation out from decoding.
//
Result(Element*) Alias_Blob_As(
    Sink(Element) out,
    const Element* blob,
    Heart as
){
    const Binary* bin = Cell_Binary(blob);

    if (as == TYPE_BLOB)  // (as blob! data) when data may be text or blob
        return Copy_Cell(out, blob);

    if (Any_Utf8_Type(as)) {  // convert to a string as first step [1]
        if (as == TYPE_WORD) {  // early fail on this, to save time
            if (Series_Index(blob) != 0)  // (vs. failing on AS WORD! of string)
                return fail ("Can't alias BLOB! as WORD! unless at head");
        }

        Size byteoffset = Series_Index(blob);

        const Byte* at_ptr = Binary_At(bin, byteoffset);
        if (Is_Continuation_Byte(*at_ptr))  // must be on codepoint start
            return fail (
                "Index must be at codepoint to convert BLOB! to ANY-STRING?"
            );

        enum Reb_Strmode strmode = STRMODE_ALL_CODEPOINTS;  // allow CR [2]

        const Strand* str;
        REBLEN index;
        if (
            not Is_Stub_Strand(bin)
            or strmode != STRMODE_ALL_CODEPOINTS
        ){
            if (not Is_Flex_Frozen(bin))
                if (Get_Cell_Flag(blob, CONST))
                    return fail (Error_Alias_Constrains_Raw());

            Length num_codepoints = 0;

            index = 0;

            Size bytes_left = Binary_Len(bin);
            const Byte* bp = Binary_Head(bin);
            for (; bytes_left > 0; --bytes_left, ++bp) {
                if (bp < at_ptr)
                    ++index;

                if (Is_Byte_Ascii(*bp))
                    Validate_Ascii_Byte(bp, strmode, Binary_Head(bin));
                else {
                    trap (
                      Codepoint c = Back_Scan_Utf8_Char(&bp, &bytes_left)
                    );
                    UNUSED(c);
                }

                ++num_codepoints;
            }
            TASTE_BYTE(m_cast(Binary*, bin)) = FLAVOR_0;  // next step sets
            m_cast(Binary*, bin)->header.bits |= STUB_MASK_STRAND;

            str = cast(Strand*, bin);

            Tweak_Link_Bookmarks(str, nullptr);
            Term_Strand_Len_Size(
                m_cast(Strand*, str),  // legal for tweaking cached data
                num_codepoints,
                Binary_Len(bin)
            );

            // !!! TBD: cache index/offset
        }
        else {  // it's a string, but doesn't accelerate offset -> index
            str = cast(Strand*, bin);
            index = 0;  // we'll count up to find the codepoint index

            Utf8(const*) cp = Strand_Head(str);
            REBLEN len = Strand_Len(str);
            while (index < len and cp != at_ptr) {  // slow walk...
                ++index;
                cp = Skip_Codepoint(cp);
            }
        }

        if (Any_String_Type(as))
            return Init_Any_String_At(out, as, str, index);

        DECLARE_ELEMENT (string);
        Init_Any_String_At(string, TYPE_TEXT, str, index);

        return Alias_Any_String_As(out, string, as);
    }

    return fail (Error_Invalid_Type(as));
}


IMPLEMENT_GENERIC(AS, Is_Blob)
{
    INCLUDE_PARAMS_OF_AS;

    Element* blob = Element_ARG(VALUE);
    Heart as = Datatype_Builtin_Heart(ARG(TYPE));

    require (
      Alias_Blob_As(OUT, blob, as)
    );
    return OUT;
}


IMPLEMENT_GENERIC(COPY, Is_Blob)
{
    INCLUDE_PARAMS_OF_COPY;

    Element* blob = Element_ARG(VALUE);
    UNUSED(ARG(DEEP));  // :DEEP is historically ignored on BLOB!

    require (
      Copy_Blob_Part_At_May_Modify_Index(OUT, blob, ARG(PART))
    );
    return OUT;
}


IMPLEMENT_GENERIC(TAKE, Is_Blob)
{
    INCLUDE_PARAMS_OF_TAKE;

    Element* blob = Element_ARG(SERIES);
    Binary* bin = Cell_Binary_Ensure_Mutable(blob);

    if (ARG(DEEP))
        panic (Error_Bad_Refines_Raw());

    REBINT len;
    if (ARG(PART)) {
        len = Part_Len_May_Modify_Index(blob, ARG(PART));
        if (len == 0)
            return Init_Blob(OUT, Make_Binary(0));
    } else
        len = 1;

    REBINT tail = Series_Len_Head(blob);  // Note :PART can change index

    if (ARG(LAST)) {
        if (tail - len < 0) {
            SERIES_INDEX_UNBOUNDED(blob) = 0;
            len = tail;
        }
        else
            SERIES_INDEX_UNBOUNDED(blob) = tail - len;
    }

    REBLEN index = Series_Index(blob);

    if (index >= tail) {
        if (not ARG(PART))
            return fail (Error_Nothing_To_Take_Raw());

        return Init_Blob(OUT, Make_Binary(0));
    }

    if (not ARG(PART))  // just return byte value
        Init_Integer(OUT, *Blob_At(blob));
    else { // return binary series
        require (
          Binary* copy = Copy_Binary_At_Len(bin, index, len)
        );
        Init_Blob(OUT, copy);
    }

    Remove_Any_Series_Len(blob, index, len);  // bad UTF-8 alias fails
    return OUT;
}


IMPLEMENT_GENERIC(REVERSE, Is_Blob)
{
    INCLUDE_PARAMS_OF_REVERSE;

    Element* blob = Element_ARG(SERIES);

    REBLEN len = Part_Len_May_Modify_Index(blob, ARG(PART));
    Byte* bp = Blob_At_Ensure_Mutable(blob);  // index may've changed

    if (len > 0) {
        REBLEN n = 0;
        REBLEN m = len - 1;
        for (; n < len / 2; n++, m--) {
            Byte b = bp[n];
            bp[n] = bp[m];
            bp[m] = b;
        }
    }
    return COPY(blob);
}


// 1. This repeats behavior for RANDOMIZE of Any_Utf8, but since not all UTF-8
//    data has a node the way BLOB! does, and the indices need translation
//    if it's a series-based UTF-8 from codepoint index to byte index... it's
//    just easier to factor it like this.
//
IMPLEMENT_GENERIC(RANDOMIZE, Is_Blob)
{
    INCLUDE_PARAMS_OF_RANDOMIZE;

    Element* blob = Element_ARG(SEED);
    possibly(Is_Stub_Strand(Cell_Binary(blob)));  // may be aliased UTF-8 [1]

    Size size;
    const Byte* data = Blob_Size_At(&size, blob);
    Set_Random(crc32_z(0L, data, size));
    return TRASH;
}


// See notes on RANDOM-PICK on whether specializations like this are worth it.
//
IMPLEMENT_GENERIC(RANDOM_PICK, Is_Blob)
{
    INCLUDE_PARAMS_OF_RANDOM_PICK;

    Element* blob = Element_ARG(COLLECTION);

    REBINT tail = Series_Len_Head(blob);
    REBINT index = Series_Index(blob);

    if (index >= tail)
        return fail (Error_Bad_Pick_Raw(Init_Integer(SPARE, 0)));

    index += Random_Int(did ARG(SECURE)) % (tail - index);
    const Binary* bin = Cell_Binary(blob);
    return Init_Integer(OUT, *Binary_At(bin, index));
}


IMPLEMENT_GENERIC(SHUFFLE, Is_Blob)
{
    INCLUDE_PARAMS_OF_SHUFFLE;

    Element* blob = Element_ARG(SERIES);

    REBINT index = Series_Index(blob);

    Binary* bin = Cell_Binary_Ensure_Mutable(blob);

    bool secure = did ARG(SECURE);
    REBLEN n;
    for (n = Binary_Len(bin) - index; n > 1;) {
        REBLEN k = index + Random_Int(secure) % n;
        n--;
        Byte swap = *Binary_At(bin, k);
        *Binary_At(bin, k) = *Binary_At(bin, n + index);
        *Binary_At(bin, n + index) = swap;
    }
    return COPY(blob);
}


IMPLEMENT_GENERIC(SIZE_OF, Is_Blob)
{
    INCLUDE_PARAMS_OF_SIZE_OF;

    Element* blob = Element_ARG(VALUE);

    Size size;
    Blob_Size_At(&size, blob);
    return Init_Integer(OUT, size);
}


// 1. While it is technically the case that a binary *might* alias a
//    string and hence already be validated, the index might not be on
//    a codepoint boundary, and it's not worth optimizing for a scan
//    of one character.
//
// 2. Zero bytes are illegal in strings, and it was deemed that #"" was
//    better as an empty rune than as a conceptual "NUL codepoint".
//    But #{00} as NUL serves some of those purposes.
//
IMPLEMENT_GENERIC(CODEPOINT_OF, Is_Blob)
{
    INCLUDE_PARAMS_OF_CODEPOINT_OF;

    Element* blob = Element_ARG(VALUE);

    Size size;
    const Byte* bp = Blob_Size_At(&size, blob);
    if (size == 1 and *bp == 0)
        return Init_Integer(OUT, 0);  // codepoint of #{00} -> 0 [2]

    trap (
      Codepoint c = Back_Scan_Utf8_Char(&bp, nullptr)
    );
    ++bp;  // Back_Scan() requires increment

    if (bp != Binary_Tail(Cell_Binary(blob)))
        return fail (Error_Not_One_Codepoint_Raw());

    return Init_Integer(OUT, c);
}


enum COMPARE_CHR_FLAGS {
    CC_FLAG_CASE = 1 << 0, // Case sensitive sort
    CC_FLAG_REVERSE = 1 << 1 // Reverse sort order
};


// This function is called by qsort_r, on behalf of the string sort
// function.  The `state` is an argument passed through from the caller
// and given to us by the sort routine, which tells us about the string
// and the kind of sort that was requested.
//
static int Qsort_Byte_Callback(void *state, const void *v1, const void *v2)
{
    Flags* flags = cast(Flags*, state);

    Byte b1 = *cast(Byte*, v1);
    Byte b2 = *cast(Byte*, v2);

    if (*flags & CC_FLAG_REVERSE)
        return b2 - b1;
    else
        return b1 - b2;
}


IMPLEMENT_GENERIC(SORT, Is_Blob)
{
    INCLUDE_PARAMS_OF_SORT;

    Element* blob = Element_ARG(SERIES);

    if (ARG(ALL))
        panic (Error_Bad_Refines_Raw());

    if (ARG(CASE)) {
        // Ignored...all BLOB! sorts are case-sensitive.
    }

    if (ARG(COMPARE))
        panic (Error_Bad_Refines_Raw());  // !!! not in R3-Alpha

    Flags flags = 0;

    Copy_Cell(OUT, blob);  // copy to output before index adjustment

    REBLEN len = Part_Len_May_Modify_Index(blob, ARG(PART));
    Byte* data_at = Blob_At_Ensure_Mutable(blob);  // ^ index changes

    if (len <= 1)
        return OUT;

    REBLEN skip;
    if (not ARG(SKIP))
        skip = 1;
    else {
        skip = Get_Num_From_Arg(unwrap ARG(SKIP));
        if (skip <= 0 or (len % skip != 0) or skip > len)
            panic (PARAM(SKIP));
    }

    Size size = 1;
    if (skip > 1) {
        len /= skip;
        size *= skip;
    }

    if (ARG(REVERSE))
        flags |= CC_FLAG_REVERSE;

    bsd_qsort_r(
        data_at,
        len,
        size,
        &flags,
        &Qsort_Byte_Callback
    );
    return OUT;
}


//
//  encode-integer: native [
//
//  "Encode integer as a Little Endian or Big Endian BLOB!, signed/unsigned"
//
//      return: [blob!]
//      num [integer!]
//      options "[<+ or +/-> <number of bytes>]"
//          [block!]
//      :LE "Encode as little-endian (default is big-endian)"
//  ]
//
DECLARE_NATIVE(ENCODE_INTEGER)
{
    INCLUDE_PARAMS_OF_ENCODE_INTEGER;

    bool little = did ARG(LE);

    Stable* options = ARG(OPTIONS);
    if (Series_Len_At(options) != 2)
        panic ("ENCODE-INTEER needs length 2 options for now");

    bool no_sign = rebUnboxBoolean(
        "switch first", options, "[",
            "'+ ['true] '+/- ['false]",
            "panic -[First ENCODE-INTEGER option must be + or +/-]-",
        "]"
    );
    REBINT num_bytes = rebUnboxInteger(
        "(match integer! second", options, ") else [",
            "panic -[Second ENCODE-INTEGER option must be an integer]-",
        "]"
    );
    if (num_bytes <= 0)
        panic ("Size for ENCODE-INTEGER encoding must be at least 1");

    // !!! Implementation is somewhat inefficient, but trying to not violate
    // the C standard and write code that is general (and may help generalize
    // with BigNum conversions as well).  Improvements welcome, but trying
    // to be correct for starters...

    Binary* bin = Make_Binary(num_bytes);

    REBINT delta = little ? 1 : -1;
    Byte* bp = Binary_Head(bin);
    if (not little)
        bp += num_bytes - 1;  // go backwards for big endian

    REBI64 i = VAL_INT64(ARG(NUM));
    if (no_sign and i < 0)
        panic ("Unsigned ENCODE-INTEGER received signed input value");

    // Negative numbers are encoded with two's complement: process we use here
    // is simple: take the absolute value, inverting each byte, add one.
    //
    bool negative = i < 0;
    if (negative)
        i = -(i);

    REBINT carry = negative ? 1 : 0;
    REBINT n = 0;
    while (n != num_bytes) {
        REBINT byte = negative ? ((i % 256) ^ 0xFF) + carry : (i % 256);
        if (byte > 0xFF) {
            assert(byte == 0x100);
            carry = 1;
            byte = 0;
        }
        else
            carry = 0;
        *bp = byte;
        bp += delta;
        i = i / 256;
        ++n;
    }
    if (i != 0)
        return rebDelegate(
            "panic [", ARG(NUM), "-[exceeds]-", rebI(num_bytes), "-[bytes]-]"
        );

    // The process of byte production of a positive number shouldn't give us
    // something with the high bit set in a signed representation.
    //
    if (not no_sign and not negative and Is_Utf8_Lead_Byte(*(bp - delta)))
        return rebDelegate(
            "panic [",
                ARG(NUM), "-[aliases a negative value with signed]-",
                "-[encoding of only]-", rebI(num_bytes), "-[bytes]-",
            "]"
        );

    Term_Binary_Len(bin, num_bytes);
    return Init_Blob(OUT, bin);
}


//
//  decode-integer: native [
//
//  "Decode BLOB! as Little Endian or Big Endian, signed/unsigned integer"
//
//      return: [integer!]
//      binary "Decoded (defaults length of binary for number of bytes)"
//          [blob!]
//      options "[<+ or +/-> <number of bytes>]"
//          [block!]
//      :LE "Decode as little-endian (default is big-endian)"
//  ]
//
DECLARE_NATIVE(DECODE_INTEGER)
//
// !!! This routine may wind up being folded into DECODE as a block-oriented
// syntax for talking to the "little endian" and "big endian" codecs, but
// giving it a unique name for now.
{
    INCLUDE_PARAMS_OF_DECODE_INTEGER;

    bool little = did ARG(LE);

    Size bin_size;
    const Byte* bin_data = Blob_Size_At(&bin_size, ARG(BINARY));

    Stable* options = ARG(OPTIONS);

    REBLEN arity = Series_Len_At(options);
    if (arity != 1 and arity != 2)
        return "panic -[DECODE-INTEGER needs length 1 or 2 options for now]-";
    bool no_sign = rebUnboxBoolean(  // signed is C keyword
        "switch first", options, "[",
            "'+ ['true] '+/- ['false]",
            "panic -[First DECODE-INTEGER option must be + or +/-]-",
        "]"
    );
    REBLEN num_bytes;
    if (arity == 1)
        num_bytes = bin_size;
    else {
        num_bytes = rebUnboxInteger(
            "(match integer! second", options, ") else [",
                "panic -[Second DECODE-INTEGER option must be an integer]-",
            "]"
        );
        if (bin_size != num_bytes)
            panic ("Input length mistmatches DECODE-INTEGER size option");
    }
    if (num_bytes <= 0) {
        //
        // !!! Should #{} empty binary be 0 or warning?  (Historically, 0.)
        //
        return "panic -[Size for DEBIN decoding must be at least 1]-";
    }

    // !!! Implementation is somewhat inefficient, but trying to not violate
    // the C standard and write code that is general (and may help generalize
    // with BigNum conversions as well).  Improvements welcome, but trying
    // to be correct for starters...

    REBINT delta = little ? -1 : 1;
    const Byte* bp = bin_data;
    if (little)
        bp += num_bytes - 1;  // go backwards

    REBINT n = num_bytes;

    if (n == 0)
        return Init_Integer(OUT, 0);  // !!! Only if we let num_bytes = 0

    // default signedness interpretation to high-bit of first byte, but
    // override if the function was called with `no_sign`
    //
    bool negative = no_sign ? false : (*bp >= 0x80);

    // Consume any leading 0x00 bytes (or 0xFF if negative).  This is just
    // a stopgap measure for reading larger-looking sizes once INTEGER! can
    // support BigNums.
    //
    while (n != 0 and *bp == (negative ? 0xFF : 0x00)) {
        bp += delta;
        --n;
    }

    // If we were consuming 0xFFs and passed to a byte that didn't have
    // its high bit set, we overstepped our bounds!  Go back one.
    //
    if (negative and n > 0 and *bp < 0x80) {
        bp += -(delta);
        ++n;
    }

    // All 0x00 bytes must mean 0 (or all 0xFF means -1 if negative)
    //
    if (n == 0) {
        if (negative) {
            assert(not no_sign);
            return Init_Integer(OUT, -1);
        }
        return Init_Integer(OUT, 0);
    }

    // Not using BigNums (yet) so max representation is 8 bytes after
    // leading 0x00 or 0xFF stripped away
    //
    if (n > 8)
        panic (Error_Out_Of_Range(ARG(BINARY)));

    REBI64 i = 0;

    // Pad out to make sure any missing upper bytes match sign
    //
    REBINT fill;
    for (fill = n; fill < 8; fill++)
        i = cast(REBI64,
            (cast(REBU64, i) << 8) | (negative ? 0xFF : 0x00)
        );

    // Use binary data bytes to fill in the up-to-8 lower bytes
    //
    while (n != 0) {
        i = cast(REBI64, (cast(REBU64, i) << 8) | *bp);
        bp += delta;
        n--;
    }

    if (no_sign and i < 0)  // may become signed via shift due to 63-bit limit
        panic (Error_Out_Of_Range(ARG(BINARY)));

    return Init_Integer(OUT, i);
}


//
//  add-to-binary: native [
//
//  "Do big-endian math on a binary blob with an integer"
//
//      return: [
//          blob!   "Same number of bytes as original"
//          error!  "error on overflow"
//      ]
//      blob [blob!]
//      delta "Can be positive or negative"
//          [integer!]
//  ]
//
DECLARE_NATIVE(ADD_TO_BINARY)
//
//    >> add-to-binary #{4B} 1
//    == #{4C}
//
//    >> add-to-binary #{FF} 1
//    ** Math or Number overflow  ; not #{FE}
//
//    >> add-to-binary #{00FF} 1
//    == #{0100}
//
//    >> add-to-binary #{0100} -1
//    == #{00FF}  ; not #{FF}, size always equals original binary size
//
// !!! This crude code originated from a user request for + and - on BLOB!.
// However, it makes a lot of assumptions about overflow, signedness, and
// endianness that would be better done as some kind of "binary math dialect".
// And certainly, one might want to add BLOB! to BLOB! etc.  Since the code
// isn't completely useless it was preserved, but taken out of + and -.
//
// !!! There's a question about how a routine like this might intersect with
// or share code with a BigInt implementation that uses similar mechanics.
{
    INCLUDE_PARAMS_OF_ADD_TO_BINARY;

    Element* blob = Element_ARG(BLOB);
    Binary* bin = Cell_Binary_Ensure_Mutable(blob);

    REBINT delta = VAL_INT32(ARG(DELTA));

    if (delta == 0)  // adding or subtracting 0 works, even #{} + 0
        return COPY(blob);

    if (Series_Len_At(blob) == 0) // add/subtract to #{} otherwise
        return fail (Error_Overflow_Raw());

    while (delta != 0) {
        REBLEN wheel = Series_Len_Head(blob) - 1;
        while (true) {
            Byte* b = Binary_At(bin, wheel);
            if (delta > 0) {
                if (*b == 255) {
                    if (wheel == Series_Index(blob))
                        return fail (Error_Overflow_Raw());

                    *b = 0;
                    --wheel;
                    continue;
                }
                ++(*b);
                --delta;
                break;
            }
            else {
                if (*b == 0) {
                    if (wheel == Series_Index(blob))
                        return fail (Error_Overflow_Raw());

                    *b = 255;
                    --wheel;
                    continue;
                }
                --(*b);
                ++delta;
                break;
            }
        }
    }
    return COPY(blob);
}
