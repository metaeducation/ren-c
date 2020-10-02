//
//  File: %sys-sequence.h
//  Summary: "Common Definitions for Immutable Interstitially-Delimited Lists"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2020 Ren-C Open Source Contributors
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
// A "Sequence" is a constrained form of list of items, separated by an
// interstitial delimiter.  The two basic forms are PATH! (separated by `/`)
// and TUPLE! (separated by `.`)
//
//     append/dup/only   ; a 3-element PATH!
//     192.168.0.1       ; a 4-element TUPLE!
//
// Both forms are allowed to contain WORD!, INTEGER!, GROUP!, BLOCK!, TEXT!,
// and TAG! elements.  They also come in SET-, GET-, and SYM- forms:
//
//     <abc>/(d e f)/[g h i]:   ; a 3-element SET-PATH!
//     :foo.1.bar               ; a 3-element GET-TUPLE!
//     @abc.(def)               ; a 2-element SYM-TUPLE!
//
// It is also legal to put BLANK! in sequence slots.  They will render
// invisibly, allowing you to begin or terminate sequences with the delimiter:
//
//     .foo.bar     ; a 3-element tuple with BLANK! in the first slot
//     1/2/3/:      ; a 4-element PATH! with BLANK! in the last slot
//
// PATH!s may contain TUPLE!s, but not vice versa.  This leads to unambiguous
// interpretation of sequences:
//
//     a.b.c/d.e.f    ; a 2-element PATH! containing 3-element TUPLEs
//     a/b/c.d/e/f    ; a 5-element PATH! with 2-element TUPLE! in the middle
//
// Sequences must contain at least two elements.  They are also immutable,
// so this constraint can be validated at creation time.  Reduced cases like
// the 2-element path `/` and the 2-element tuple `.` have special handling
// that allows them to store a hidden WORD! and binding, which lets them be
// used in the evaluator as functions.
//
// The immutability of sequences allows important optimizations in the
// implementation of sequences that minimize allocations.  For instance, the
// 2-element PATH! of `/foo` can be specially encoded to use no more space
// than a plain WORD!.  There are also optimizations for encoding short
// numeric sequences like IP addresses or colors into cells.
//


// The Try_Init_Any_Sequence_XXX variants will return nullptr if any of the
// requested path elements are not valid.
//
inline static bool Is_Valid_Path_Element(const RELVAL *v) {
    return IS_BLANK(v)
        or IS_INTEGER(v)
        or IS_WORD(v)
        or IS_TUPLE(v)
        or IS_GROUP(v)
        or IS_BLOCK(v)
        or IS_TEXT(v)
        or IS_TAG(v);
}


//=//// UNCOMPRESSED ARRAY SEQUENCE FORM //////////////////////////////////=//

#define Try_Init_Any_Sequence_Arraylike(v,k,a) \
    Try_Init_Any_Sequence_At_Arraylike_Core((v), (k), (a), 0, nullptr)

#define Try_Init_Path_Arraylike(v,a) \
    Try_Init_Any_Sequence_Arraylike((v), REB_PATH, (a))


//=//// ALL-BLANK! SEQUENCE OPTIMIZATION //////////////////////////////////=//
//
// The `/` path maps to the 2-element array [_ _].  But to save on storage,
// no array is used and paths of this form are always optimized into a single
// cell.  Though the cell reports its VAL_TYPE() as a PATH!, it uses the
// underlying contents of a word cell...which makes it pick up and carry
// bindings.  That allows it to be bound to a function that runs divide.

inline static REBVAL *Init_Any_Sequence_1(RELVAL *out, enum Reb_Kind kind) {
    if (ANY_PATH_KIND(kind))
        Init_Word(out, PG_Slash_1_Canon);
    else {
        assert(ANY_TUPLE_KIND(kind));
        Init_Word(out, PG_Dot_1_Canon);
    }
    mutable_KIND_BYTE(out) = kind;  // leave MIRROR_BYTE as REB_WORD
    return SPECIFIC(out);
}


//=//// Leading-BLANK! SEQUENCE OPTIMIZATION //////////////////////////////=//
//
// Ren-C has no REFINEMENT! datatype, so `/foo` is a PATH!, which generalizes
// to where `/foo/bar` is a PATH! as well, etc.
//
// !!! Optimizations are planned to allow single element paths to fit in just
// *one* array cell.  This will make use of the fourth header byte, to
// encode when the type byte is a container for what is inside.  Use of this
// routine to mutate cells into refinements marks places where that will
// be applied.

inline static REBVAL *Try_Leading_Blank_Pathify(
    REBVAL *v,
    enum Reb_Kind kind
){
    assert(ANY_SEQUENCE_KIND(kind));

    if (IS_BLANK(v))
        return Init_Any_Sequence_1(v, kind);

    if (not Is_Valid_Path_Element(v))
        return nullptr;

    // !!! Start by just optimizing refinements as a proof-of-concept, and
    // to get efficiency parity with R3-Alpha for that situation.  Should
    // be able to apply to more types (and possibly take in things like
    // `'foo` to make `/('foo)` with an artificial GROUP!).  Review.
    //
    if (VAL_TYPE(v) == REB_WORD) {
        assert(MIRROR_BYTE(v) == REB_WORD);
        mutable_KIND_BYTE(v) = kind;
        return v;
    }

    REBARR *a = Make_Array(2);  // optimize with pairlike storage!
    Init_Blank(Alloc_Tail_Array(a));
    Move_Value(Alloc_Tail_Array(a), v);
    Freeze_Array_Shallow(a);

    REBVAL *check = Try_Init_Any_Sequence_Arraylike(v, kind, a);
    assert(check);
    UNUSED(check);

    return v;
}

inline static REBVAL *Refinify(REBVAL *v) {
    bool success = (Try_Leading_Blank_Pathify(v, REB_PATH) != nullptr);
    assert(success);
    return v;
}

inline static bool IS_REFINEMENT_CELL(REBCEL(const*) v)
  { return CELL_TYPE(v) == REB_PATH and MIRROR_BYTE(v) == REB_WORD; }

inline static bool IS_REFINEMENT(const RELVAL *v)
  { return IS_PATH(v) and MIRROR_BYTE(v) == REB_WORD; }

inline static REBSTR *VAL_REFINEMENT_SPELLING(REBCEL(const*) v) {
    assert(IS_REFINEMENT_CELL(v));
    return VAL_WORD_SPELLING(v);
}


//=//// 2-Element "PAIR" SEQUENCE OPTIMIZATION ////////////////////////////=//
//
// !!! Making paths out of two items is intended to be optimized as well,
// using the "pairing" nodes.  This should eliminate the need for a separate
// REB_PAIR type, making PAIR! just a type constraint on TUPLE!s.

inline static REBVAL *Try_Init_Any_Sequence_Pairlike(
    RELVAL *out,
    enum Reb_Kind kind,
    const REBVAL *v1,
    const REBVAL *v2
){
    if (IS_BLANK(v1))
        return Try_Leading_Blank_Pathify(Move_Value(out, v2), kind);

    REBARR *a = Make_Array(2);
    Move_Value(ARR_AT(a, 0), v1);
    Move_Value(ARR_AT(a, 1), v2);
    TERM_ARRAY_LEN(a, 2);
    Freeze_Array_Shallow(a);
    return Try_Init_Any_Sequence_Arraylike(out, kind, a);

}


//=//// BYTE-SIZED INTEGER! SEQUENCE OPTIMIZATION /////////////////////////=//
//
// Rebol's historical TUPLE! was limited to a compact form of representing
// byte-sized integers in a cell.  That optimization is used when possible,
// either when initialization is called explicitly with a byte buffer or
// when it is detected as applicable to a generated TUPLE!.
//
// This allows 8 single-byte integers to fit in a cell on 32-bit platforms,
// and 16 single-byte integers on 64-bit platforms.  If that is not enough
// space, then an array is allocated.
//
// !!! Since arrays use full cells for INTEGER! values, it would be more
// optimal to allocate an immutable binary series for larger allocations.
// This will likely be easy to reuse in an ISSUE!+CHAR! unification, so
// revisit this low-priority idea at that time.

inline static REBVAL *Init_Any_Sequence_Bytes(
    RELVAL *out,
    enum Reb_Kind kind,
    const REBYTE *data,
    REBLEN len
){
    if (len > sizeof(EXTRA(Bytes, out).common)) {  // use plain array for now
        REBARR *a = Make_Array_Core(len, NODE_FLAG_MANAGED);
        for (; len > 0; --len, ++data)
            Init_Integer(Alloc_Tail_Array(a), *data);

        Init_Block(out, Freeze_Array_Shallow(a));
    }
    else {
        REBLEN n = len;
        REBYTE *bp = PAYLOAD(Bytes, out).common;
        for (; n > 0; --n, ++data, ++bp)
            *bp = *data;
        RESET_CELL(out, REB_CHAR, CELL_MASK_NONE);
    }

    mutable_KIND_BYTE(out) = kind;  // "veneer" over "heart" type
    return cast(REBVAL*, out);
}

#define Init_Tuple_Bytes(out,data,len) \
    Init_Any_Sequence_Bytes((out), REB_TUPLE, (data), (len));

inline static REBVAL *Try_Init_Any_Sequence_All_Integers(
    RELVAL *out,
    enum Reb_Kind kind,
    const RELVAL *head,
    REBLEN len
){
  #if !defined(NDEBUG)
    Init_Unreadable_Void(out);  // not used for "blaming" a non-integer
  #endif

    if (len > sizeof(PAYLOAD(Bytes, out)).common)
        return nullptr;  // no optimization yet if won't fit in payload bytes

    RESET_CELL(out, kind, CELL_MASK_NONE);

    REBYTE *bp = PAYLOAD(Bytes, out).common;

    const RELVAL *item = head;
    REBLEN n;
    for (n = 0; n < len; ++n, ++item, ++bp) {
        if (not IS_INTEGER(item))
            return nullptr;
        REBI64 i64 = VAL_INT64(item);
        if (i64 < 0 or i64 > 255)
            return nullptr;  // only packing byte form for now
        *bp = cast(REBYTE, i64);
    }

    EXTRA(Any, out).u = len;

    mutable_MIRROR_BYTE(out) = REB_CHAR;

    return SPECIFIC(out);
}


// This is a general utility for turning stack values into something that is
// either pathlike or value like.  It is used in COMPOSE of paths, which
// allows things like:
//
//     >> compose (null)/a
//     == a
//
//     >> compose (try null)/a
//     == /a
//
//     >> compose (null)/(null)/(null)
//     ; null
//
// Not all clients will want to be this lenient, but that lack of lenience
// should be done by calling this generic routine and raising an error if
// it's not a PATH!...because the optimizations on special cases are all
// in this code.
//
inline static REBVAL *Try_Pop_Path_Or_Element_Or_Nulled(
    RELVAL *out,  // will be the error-triggering value if nullptr returned
    enum Reb_Kind kind,
    REBDSP dsp_orig
){
    assert(not IN_DATA_STACK_DEBUG(out));

    if (DSP == dsp_orig)
        return Init_Nulled(out);

    if (DSP - 1 == dsp_orig) {  // only one item, use as-is if possible
        if (not Is_Valid_Path_Element(DS_TOP))
            return nullptr;

        Move_Value(out, DS_TOP);
        DS_DROP();

        if (kind != REB_PATH) {  // carry over : or @ decoration (if possible)
            if (
                not IS_WORD(out)
                and not IS_BLOCK(out)
                and not IS_GROUP(out)
                and not IS_BLOCK(out)
                and not IS_TUPLE(out)  // !!! TBD, will support decoration
            ){
                // !!! `out` is reported as the erroring element for why the
                // path is invalid, but this would be valid in a path if we
                // weren't decorating it...rethink how to error on this.
                //
                return nullptr;
            }

            if (kind == REB_SET_PATH)
                Setify(SPECIFIC(out));
            else if (kind == REB_GET_PATH)
                Getify(SPECIFIC(out));
            else if (kind == REB_SYM_PATH)
                Symify(SPECIFIC(out));
        }
        
        return SPECIFIC(out);  // valid path element, but it's standing alon
    }

    if (DSP - dsp_orig == 2) {  // two-element path optimization
        if (not Try_Init_Any_Sequence_Pairlike(
            out,
            kind,
            DS_TOP - 1,
            DS_TOP
        )){
            DS_DROP_TO(dsp_orig);
            return nullptr;
        }

        DS_DROP_TO(dsp_orig);
        return SPECIFIC(out);
    }

    // Attempt optimization for all-INTEGER! tuple or path, e.g. IP addresses
    // (192.0.0.1) or RGBA color constants 255.0.255.  If optimization fails,
    // use normal array.
    //
    if (Try_Init_Any_Sequence_All_Integers(
        out,
        kind,
        DS_AT(dsp_orig) + 1,
        DSP - dsp_orig
    )){
        DS_DROP_TO(dsp_orig);
        return SPECIFIC(out);
    }

    REBARR *a = Pop_Stack_Values(dsp_orig);
    Freeze_Array_Shallow(a);
    if (not Try_Init_Any_Sequence_Arraylike(out, kind, a))
        return nullptr;

    return SPECIFIC(out);
}


// Note that paths can be initialized with an array, which they will then
// take as immutable...or you can create a `/foo`-style path in a more
// optimized fashion using Refinify()

inline static REBLEN VAL_SEQUENCE_LEN(REBCEL(const*) sequence) {
    assert(ANY_SEQUENCE_KIND(CELL_TYPE(sequence)));

    if (MIRROR_BYTE(sequence) == REB_WORD)
        return 2;  // simulated 2-blanks sequence

    if (MIRROR_BYTE(sequence) == REB_CHAR)
        return EXTRA(Any, sequence).u;  // cell-packed byte-oriented sequence

    REBARR *a = ARR(VAL_NODE(sequence));
    assert(ARR_LEN(a) >= 2);
    assert(Is_Array_Frozen_Shallow(a));
    return ARR_LEN(a);
}

// !!! This is intended to return either a pairing node or an array node.
// If it is a pairing it will not be terminated.  Either way, it usually
// only represents the non-BLANK! contents of the path...blank contents are
// compressed by means of the second payload slot, which counts the number
// of blanks at the head and the tail.
//
inline static const REBNOD *VAL_SEQUENCE_NODE(REBCEL(const*) sequence) {
    assert(ANY_SEQUENCE_KIND(CELL_TYPE(sequence)));
    assert(ANY_SEQUENCE_KIND(MIRROR_BYTE(sequence)));

    const REBNOD *n = VAL_NODE(sequence);
    assert(not (FIRST_BYTE(n) & NODE_BYTEMASK_0x01_CELL));  // !!! not yet...
    return n;
}

// Paths may not always be implemented as arrays, so this mechanism needs to
// be used to read the pointers.  If the value is not in an array, it may
// need to be written to a passed-in storage location.
//
inline static const RELVAL *VAL_SEQUENCE_AT(
    RELVAL *store,  // return result may or may not point at this cell
    REBCEL(const*) sequence,
    REBLEN n
){
    assert(store != sequence);  // cannot be the same
  #if !defined(NDEBUG)
    Init_Unreadable_Void(store);  // catch store use in case we don't write it
  #endif

    enum Reb_Kind kind = CELL_TYPE(sequence);  // Not *CELL_KIND*, may be word
    assert(ANY_SEQUENCE_KIND(kind));

    if (MIRROR_BYTE(sequence) == REB_WORD) {
        assert(n < 2);
 
        if (
            n == 0
            or VAL_STRING(sequence) == PG_Dot_1_Canon
            or VAL_STRING(sequence) == PG_Slash_1_Canon
        ){
            return BLANK_VALUE;
        }

        // Because the cell is being viewed as a PATH!, we cannot view it as
        // a WORD! also unless we fiddle the bits at a new location.
        //
        Blit_Cell(store, CELL_TO_VAL(sequence));
        mutable_KIND_BYTE(store) = REB_WORD;
        return store;
    }

    if (MIRROR_BYTE(sequence) == REB_CHAR) {
        assert(n < EXTRA(Any, sequence).u);
        return Init_Integer(store, PAYLOAD(Bytes, sequence).common[n]);
    }

    REBARR *a = ARR(VAL_NODE(sequence));
    assert(ARR_LEN(a) >= 2);
    assert(Is_Array_Frozen_Shallow(a));
    return ARR_AT(a, n);
}

inline static REBYTE VAL_SEQUENCE_BYTE_AT(REBCEL(const*) path, REBLEN n)
{
    DECLARE_LOCAL (temp);
    const RELVAL *at = VAL_SEQUENCE_AT(temp, path, n);
    return VAL_UINT8(at);  // !!! All callers of this routine need vetting
}

inline static REBSPC *VAL_SEQUENCE_SPECIFIER(const RELVAL *sequence)
{
    enum Reb_Kind kind = CELL_TYPE(sequence);  // not *CELL_KIND*, may be word
    assert(ANY_SEQUENCE_KIND(kind));

    switch (MIRROR_BYTE(sequence)) {
      case REB_CHAR:
      case REB_WORD:
        return SPECIFIED;

      case REB_PATH:
        return VAL_SPECIFIER(sequence);

      default:
        assert(false);
        DEAD_END;
    }
}


// !!! This is a simple compatibility routine for all the tuple-using code
// that was hanging around before (IMAGE!, networking) which assumed that
// tuples could only contain byte-sized integers.  All callsites referring
// to it are transitional.
//
inline static bool Did_Get_Sequence_Bytes(
    void *buf,
    const RELVAL *sequence,
    REBSIZ buf_size
){
    REBLEN len = VAL_SEQUENCE_LEN(sequence);

    REBYTE *dp = cast(REBYTE*, buf);
    REBSIZ i;
    DECLARE_LOCAL (temp);
    for (i = 0; i < buf_size; ++i) {
        if (i >= len) {
            dp[i] = 0;
            continue;
        }
        const RELVAL *at = VAL_SEQUENCE_AT(temp, sequence, i);
        if (not IS_INTEGER(at))
            return false;
        REBI64 i64 = VAL_INT64(at);
        if (i64 < 0 or i64 > 255)
            return false;

        dp[i] = cast(REBYTE, i64);
    }
    return true;
}

inline static void Get_Tuple_Bytes(
    void *buf,
    const RELVAL *tuple,
    REBSIZ buf_size
){
    assert(IS_TUPLE(tuple));
    if (not Did_Get_Sequence_Bytes(buf, tuple, buf_size))
        fail ("non-INTEGER! found used with Get_Tuple_Bytes()");
}

#define MAX_TUPLE \
    ((sizeof(uint32_t) * 2))  // !!! No longer a "limit", review callsites
