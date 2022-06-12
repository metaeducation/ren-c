//
//  File: %sys-sequence.h
//  Summary: "Common Definitions for Immutable Interstitially-Delimited Lists"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2022 Ren-C Open Source Contributors
// Copyright 2012 REBOL Technologies
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
// A "Sequence" is a constrained type of item list, with elements separated by
// interstitial delimiters.  The two basic forms are PATH! (separated by `/`)
// and TUPLE! (separated by `.`)
//
//     append/dup/only   ; a 3-element PATH!
//     192.168.0.1       ; a 4-element TUPLE!
//
// Because they are defined by separators *between* elements, sequences of
// zero or one item are not legal.  This is one reason why they are immutable:
// so the constraint of having at least two items can be validated at the time
// of creation.
//
// Both forms are allowed to contain WORD!, INTEGER!, GROUP!, BLOCK!, TEXT!,
// BAD-WORD!, and TAG! elements.  There are SET-, GET-, META-, and THE- forms:
//
//     <abc>/(d e f)/[g h i]:   ; a 3-element SET-PATH!
//     :foo.1.bar               ; a 3-element GET-TUPLE!
//     ^abc.(def)               ; a 2-element META-TUPLE!
//     @<a>/<b>/<c>             ; a 3-element THE-TUPLE!
//
// It is also legal to put BLANK! in sequence slots.  They will render
// invisibly, allowing you to begin or terminate sequences with the delimiter:
//
//     .foo.bar     ; a 3-element TUPLE! with BLANK! in the first slot
//     1/2/3/:      ; a 4-element PATH! with BLANK! in the last slot
//     /            ; a 2-element PATH! with BLANK! in the first and last slot
//
// PATH!s may contain TUPLE!s, but not vice versa.  This means that mixed
// usage can be interpreted unambiguously:
//
//     a.b.c/d.e.f    ; a 2-element PATH! containing 3-element TUPLEs
//     a/b/c.d/e/f    ; a 5-element PATH! with 2-element TUPLE! in the middle
//
// Neither PATH! nor TUPLE may contain "arrow-words" in any slot (those with
// `>` or `<` in their spelling), so interpretation of TAG!s is unambiguous:
//
//     ..<..>..     ; a 5-element TUPLE! with TAG! <..> in slot 3, rest BLANK!
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * Reduced cases like the 2-element path `/` and the 2-element tuple `.`
//   are considered to be WORD!.  This was considered non-negotiable, that
//   `/` be allowed to mean divide.  Making it a PATH! that ran code turned
//   out to be much more convoluted than having special word flags.  (See
//   SYMBOL_FLAG_ESCAPE_XXX for how these words are handled "gracefully".)
//
// * The immutability of sequences allows important optimizations in the
//   implementation that minimize allocations.  For instance, the 2-element
//   PATH! of `/foo` can be specially encoded to use no more space
//   than a plain WORD!.
//
//   (There are also optimizations for encoding short numeric sequences like IP
//   addresses or colors into single cells...which aren't as important but
//   carried over to preserve history of the feature.)
//
// * Compressed forms detect their compression as follows:
//
//   - Byte compressed forms do not have CELL_FLAG_SEQUENCE_HAS_NODE
//
//   - Pair compression (TBD) would have the first node with NODE_FLAG_CELL
//
//   - Single WORD! forms have the first node as FLAVOR_SYMBOL
//        If CELL_FLAG_REFINEMENT_LIKE it is either a `/foo` or `.foo` case
//        Without the flag, it is either a `foo/` or `foo.` case
//
//   - Uncompressed forms have the first node as FLAVOR_ARRAY
//
// !!! More ambitious compression could be pursued, especially since once an
// array form is aliased to a path it can no longer be mutated.  So any slots
// pertinent to mutation properties could be reused to indicate a compressed
// form.  But this is really low priority.
//

inline static bool Is_Valid_Sequence_Element(
    enum Reb_Kind sequence_kind,
    const Cell *v
){
    assert(ANY_SEQUENCE_KIND(sequence_kind));

    enum Reb_Kind k = VAL_TYPE(v);
    if (
        k == REB_BLANK
        or k == REB_INTEGER
        or k == REB_GROUP
        or k == REB_BLOCK
        or k == REB_TEXT
        or k == REB_TAG
        or k == REB_WORD
        or k == REB_BAD_WORD  // legal, e.g. `~/home/Projects/ren-c/README.md`
    ){
        return true;
    }

    if (k == REB_TUPLE)  // PATH! can have TUPLE!, not vice-versa
        return ANY_PATH_KIND(sequence_kind);

    return false;
}


// The Try_Init_Any_Sequence_XXX variants will return nullptr if any of the
// requested path elements are not valid.  Instead of an initialized sequence,
// the output cell passed in will be either a REB_NULL (if the data was
// too short) or it will be the first badly-typed value that was problematic.
//
inline static REBCTX *Error_Bad_Sequence_Init(const REBVAL *v) {
    if (IS_NULLED(v))
        return Error_Sequence_Too_Short_Raw();
    fail (Error_Bad_Sequence_Item_Raw(v));
}


//=//// UNCOMPRESSED ARRAY SEQUENCE FORM //////////////////////////////////=//

#define Try_Init_Any_Sequence_Arraylike(v,k,a) \
    Try_Init_Any_Sequence_At_Arraylike_Core((v), (k), (a), SPECIFIED, 0)

#define Try_Init_Path_Arraylike(v,a) \
    Try_Init_Any_Sequence_Arraylike((v), REB_PATH, (a))


//=//// ALL-BLANK! SEQUENCE OPTIMIZATION //////////////////////////////////=//
//
// At one time, the `/` path mapped to the 2-element array [_ _], and there
// was a storage optimization here which put it into a single cell that was
// a WORD! under the hood (with a PATH! veneer).  Same with `.` as a TUPLE!.
// This was done for the sake of preventing the creation of a WORD! which
// would be ambiguous if put in a PATH! or TUPLE!.
//
// But people still wanted `/` for division, and getting the mutant path to
// act like a WORD! was too much of a hassle vs. just saying that the words
// would be escaped if used in tuples or paths, like `obj.|/|`.  So the
// mechanics that optimized as a word were just changed to make a real WORD!
// with SYMBOL_FLAG_ESCAPE_IN_SEQUENCE.
//
inline static REBVAL *Init_Any_Sequence_1(Cell *out, enum Reb_Kind kind) {
    if (ANY_PATH_KIND(kind))
        Init_Word(out, Canon(SLASH_1));
    else {
        assert(ANY_TUPLE_KIND(kind));
        Init_Word(out, Canon(DOT_1));
    }
    return cast(REBVAL*, out);
}


//=//// Leading-BLANK! SEQUENCE OPTIMIZATION //////////////////////////////=//
//
// Ren-C has no REFINEMENT! datatype, so `/foo` is a PATH!, which generalizes
// to where `/foo/bar` is a PATH! as well, etc.
//
// In order to make this not cost more than a REFINEMENT! ANY-WORD! did in
// R3-Alpha, the underlying representation of `/foo` in the cell is the same
// as an ANY-WORD!.

inline static REBVAL *Try_Leading_Blank_Pathify(
    REBVAL *v,
    enum Reb_Kind kind
){
    assert(ANY_SEQUENCE_KIND(kind));

    if (IS_BLANK(v))
        return Init_Any_Sequence_1(v, kind);

    if (not Is_Valid_Sequence_Element(kind, v))
        return nullptr;  // leave element in v to indicate "the bad element"

    // See notes at top of file regarding optimizing `/a` into a single cell.
    //
    enum Reb_Kind inner_kind = VAL_TYPE(v);
    if (inner_kind == REB_WORD) {
        SET_CELL_FLAG(v, REFINEMENT_LIKE);
        mutable_HEART_BYTE(v) = kind;
        return v;
    }

    REBARR *a = Make_Array_Core(
        2,  // TBD: optimize "pairlike" to use a pairing node
        NODE_FLAG_MANAGED
    );
    Init_Blank(Alloc_Tail_Array(a));
    Copy_Cell(Alloc_Tail_Array(a), v);
    Freeze_Array_Shallow(a);

    Init_Block(v, a);
    mutable_HEART_BYTE(v) = kind;

    return v;
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
    Cell *out,
    enum Reb_Kind kind,
    const REBYTE *data,
    REBSIZ size
){
    Reset_Cell_Header_Untracked(out, kind, CELL_MASK_NONE);
    mutable_BINDING(out) = nullptr;  // paths are bindable, can't have garbage

    if (size > sizeof(PAYLOAD(Bytes, out).at_least_8) - 1) {  // too big
        REBARR *a = Make_Array_Core(size, NODE_FLAG_MANAGED);
        for (; size > 0; --size, ++data)
            Init_Integer(Alloc_Tail_Array(a), *data);

        Init_Block(out, Freeze_Array_Shallow(a));  // !!! TBD: compact BINARY!
    }
    else {
        PAYLOAD(Bytes, out).at_least_8[IDX_SEQUENCE_USED] = size;
        REBYTE *dest = PAYLOAD(Bytes, out).at_least_8 + 1;
        for (; size > 0; --size, ++data, ++dest)
            *dest = *data;
    }

    return cast(REBVAL*, out);
}

#define Init_Tuple_Bytes(out,data,len) \
    Init_Any_Sequence_Bytes((out), REB_TUPLE, (data), (len));

inline static REBVAL *Try_Init_Any_Sequence_All_Integers(
    Cell *out,
    enum Reb_Kind kind,
    const Cell *head,  // NOTE: Can't use DS_PUSH() or evaluation
    REBLEN len
){
    if (len > sizeof(PAYLOAD(Bytes, out)).at_least_8 - 1)
        return nullptr;  // no optimization yet if won't fit in payload bytes

    if (len < 2)
        return nullptr;

    Reset_Cell_Header_Untracked(out, kind, CELL_MASK_NONE);
    mutable_BINDING(out) = nullptr;  // paths are bindable, can't be garbage

    PAYLOAD(Bytes, out).at_least_8[IDX_SEQUENCE_USED] = len;

    REBYTE *bp = PAYLOAD(Bytes, out).at_least_8 + 1;

    const Cell *item = head;
    REBLEN n;
    for (n = 0; n < len; ++n, ++item, ++bp) {
        if (not IS_INTEGER(item))
            return nullptr;
        REBI64 i64 = VAL_INT64(item);
        if (i64 < 0 or i64 > 255)
            return nullptr;  // only packing byte form for now
        *bp = cast(REBYTE, i64);
    }

    return cast(REBVAL*, out);
}


//=//// 2-Element "PAIR" SEQUENCE OPTIMIZATION ////////////////////////////=//
//
// !!! Making paths out of two items is intended to be optimized as well,
// using the "pairing" nodes.  This should eliminate the need for a separate
// REB_PAIR type, making PAIR! just a type constraint on TUPLE!s.

inline static REBVAL *Try_Init_Any_Sequence_Pairlike_Core(
    Cell *out,
    enum Reb_Kind kind,
    const Cell *v1,
    const Cell *v2,
    REBSPC *specifier  // assumed to apply to both v1 and v2
){
    if (IS_BLANK(v1))
        return Try_Leading_Blank_Pathify(
            Derelativize(out, v2, specifier),
            kind
        );

    if (not Is_Valid_Sequence_Element(kind, v1)) {
        Derelativize(out, v1, specifier);
        return nullptr;
    }

    // See notes at top of file regarding optimizing `/a` and `.a`
    //
    enum Reb_Kind inner = VAL_TYPE(v1);
    if (IS_BLANK(v2) and inner == REB_WORD) {
        Derelativize(out, v1, specifier);
        mutable_HEART_BYTE(out) = kind;
        return cast(REBVAL*, out);
    }

    if (IS_INTEGER(v1) and IS_INTEGER(v2)) {
        REBYTE buf[2];
        REBI64 i1 = VAL_INT64(v1);
        REBI64 i2 = VAL_INT64(v2);
        if (i1 >= 0 and i2 >= 0 and i1 <= 255 and i2 <= 255) {
            buf[0] = cast(REBYTE, i1);
            buf[1] = cast(REBYTE, i2);
            return Init_Any_Sequence_Bytes(out, kind, buf, 2);
        }

        // fall through
    }

    if (not Is_Valid_Sequence_Element(kind, v2)) {
        Derelativize(out, v2, specifier);
        return nullptr;
    }

    REBARR *a = Make_Array_Core(
        2,
        NODE_FLAG_MANAGED  // optimize "pairlike"
    );
    Derelativize(ARR_AT(a, 0), v1, specifier);
    Derelativize(ARR_AT(a, 1), v2, specifier);
    SET_SERIES_LEN(a, 2);
    Freeze_Array_Shallow(a);

    Init_Block(out, a);
    mutable_HEART_BYTE(out) = kind;
    return cast(REBVAL*, out);
}

#define Try_Init_Any_Sequence_Pairlike(out,kind,v1,v2) \
    Try_Init_Any_Sequence_Pairlike_Core((out), (kind), (v1), (v2), SPECIFIED)


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
inline static REBVAL *Try_Pop_Sequence_Or_Element_Or_Nulled(
    Cell *out,  // will be the error-triggering value if nullptr returned
    enum Reb_Kind kind,
    REBDSP dsp_orig
){
    if (DSP == dsp_orig)
        return Init_Nulled(out);

    if (DSP - 1 == dsp_orig) {  // only one item, use as-is if possible
        if (not Is_Valid_Sequence_Element(kind, DS_TOP))
            return nullptr;

        Copy_Cell(out, DS_TOP);
        DS_DROP();

        if (kind != REB_PATH) {  // carry over : or ^ decoration (if possible)
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
            else if (kind == REB_META_PATH)
                Metafy(SPECIFIC(out));
        }

        return cast(REBVAL*, out);  // valid path element, standing alone
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
        return cast(REBVAL*, out);
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
        return cast(REBVAL*, out);
    }

    REBARR *a = Pop_Stack_Values_Core(dsp_orig, NODE_FLAG_MANAGED);
    Freeze_Array_Shallow(a);
    if (not Try_Init_Any_Sequence_Arraylike(out, kind, a))
        return nullptr;

    return cast(REBVAL*, out);
}


// Note that paths can be initialized with an array, which they will then
// take as immutable...or you can create a `/foo`-style path in a more
// optimized fashion using Refinify()

inline static REBLEN VAL_SEQUENCE_LEN(noquote(const Cell*) sequence) {
    assert(ANY_SEQUENCE_KIND(CELL_HEART(sequence)));

    if (NOT_CELL_FLAG(sequence, SEQUENCE_HAS_NODE)) {  // compressed bytes
        assert(NOT_CELL_FLAG(sequence, SECOND_IS_NODE));
        return PAYLOAD(Bytes, sequence).at_least_8[IDX_SEQUENCE_USED];
    }

    const REBNOD *node1 = VAL_NODE1(sequence);
    if (NODE_BYTE(node1) & NODE_BYTEMASK_0x01_CELL) {  // see if it's a pairing
        assert(false);  // these don't exist yet
        return 2;  // compressed 2-element sequence
    }

    switch (SER_FLAVOR(SER(node1))) {
      case FLAVOR_SYMBOL :  // compressed single WORD! sequence
        return 2;

      case FLAVOR_ARRAY : {  // uncompressed sequence
        REBARR *a = ARR(VAL_NODE1(sequence));
        assert(ARR_LEN(a) >= 2);
        assert(Is_Array_Frozen_Shallow(a));
        return ARR_LEN(a); }

      default :
        assert(false);
        DEAD_END;
    }
}

// Paths may not always be implemented as arrays, so this mechanism needs to
// be used to read the pointers.  If the value is not in an array, it may
// need to be written to a passed-in storage location.
//
// NOTE: It's important that the return result from this routine be a Cell*
// and not a REBVAL*, because path ATs are relative values.  Hence the
// seemingly minor optimization of not copying out array cells is more than
// just that...it also assures that the caller isn't passing in a REBVAL*
// and then using it as if it were fully specified.  It serves two purposes.
//
inline static const Cell *VAL_SEQUENCE_AT(
    Cell *store,  // return may not point at this cell, ^-- SEE WHY!
    noquote(const Cell*) sequence,
    REBLEN n
){
    assert(store != sequence);
    assert(ANY_SEQUENCE_KIND(CELL_HEART(sequence)));

    if (NOT_CELL_FLAG(sequence, SEQUENCE_HAS_NODE)) {  // compressed bytes
        assert(n < PAYLOAD(Bytes, sequence).at_least_8[IDX_SEQUENCE_USED]);
        return Init_Integer(store, PAYLOAD(Bytes, sequence).at_least_8[n + 1]);
    }

    const REBNOD *node1 = VAL_NODE1(sequence);
    if (NODE_BYTE(node1) & NODE_BYTEMASK_0x01_CELL) {  // test if it's a pairing
        assert(false);  // these don't exist yet
        return nullptr;  // compressed 2-element sequence
    }

    switch (SER_FLAVOR(SER(node1))) {
      case FLAVOR_SYMBOL : {  // compressed single WORD! sequence
        assert(n < 2);
        if (GET_CELL_FLAG(sequence, REFINEMENT_LIKE) ? n == 0 : n != 0)
            return Lib(BLANK);

        // Because the cell is being viewed as a PATH!, we cannot view it as
        // a WORD! also unless we fiddle the bits at a new location.
        //
        if (sequence != store)
            Copy_Cell(store, CELL_TO_VAL(sequence));
        mutable_HEART_BYTE(store) = REB_WORD;
        mutable_QUOTE_BYTE(store) = 0;  // quote is actually "on" the sequence
        return store; }

      case FLAVOR_ARRAY : {  // uncompressed sequence
        const REBARR *a = ARR(VAL_NODE1(sequence));
        assert(ARR_LEN(a) >= 2);
        assert(Is_Array_Frozen_Shallow(a));
        return ARR_AT(a, n); }  // array is read only

      default :
        assert(false);
        DEAD_END;
    }
}

inline static REBYTE VAL_SEQUENCE_BYTE_AT(
    noquote(const Cell*) sequence,
    REBLEN n
){
    DECLARE_LOCAL (temp);
    const Cell *at = VAL_SEQUENCE_AT(temp, sequence, n);
    if (not IS_INTEGER(at))
        fail ("VAL_SEQUENCE_BYTE_AT() used on non-byte ANY-SEQUENCE!");
    return VAL_UINT8(at);  // !!! All callers of this routine need vetting
}

inline static REBSPC *VAL_SEQUENCE_SPECIFIER(
    noquote(const Cell*) sequence
){
    assert(ANY_SEQUENCE_KIND(CELL_HEART(sequence)));

    // Getting the specifier for any of the optimized types means getting
    // the specifier for *that item in the sequence*; the sequence itself
    // does not provide a layer of communication connecting the insides
    // to a frame instance (because there is no actual layer).

    if (NOT_CELL_FLAG(sequence, SEQUENCE_HAS_NODE))  // compressed bytes
        return SPECIFIED;

    const REBNOD *node1 = VAL_NODE1(sequence);
    if (NODE_BYTE(node1) & NODE_BYTEMASK_0x01_CELL) {  // see if it's a pairing
        assert(false);  // these don't exist yet
        return SPECIFIED;  // compressed 2-element sequence
    }

    switch (SER_FLAVOR(SER(node1))) {
      case FLAVOR_SYMBOL :  // compressed single WORD! sequence
        return SPECIFIED;

      case FLAVOR_ARRAY :  // uncompressed sequence
        return VAL_SPECIFIER(sequence);

      default :
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
    const Cell *sequence,
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
        const Cell *at = VAL_SEQUENCE_AT(temp, sequence, i);
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
    const Cell *tuple,
    REBSIZ buf_size
){
    assert(IS_TUPLE(tuple));
    if (not Did_Get_Sequence_Bytes(buf, tuple, buf_size))
        fail ("non-INTEGER! found used with Get_Tuple_Bytes()");
}

#define MAX_TUPLE \
    ((sizeof(uint32_t) * 2))  // !!! No longer a "limit", review callsites



//=//// REFINEMENTS AND PREDICATES ////////////////////////////////////////=//

inline static REBVAL *Refinify(REBVAL *v) {
    bool success = (Try_Leading_Blank_Pathify(v, REB_PATH) != nullptr);
    assert(success);
    UNUSED(success);
    return v;
}

inline static bool IS_REFINEMENT_CELL(noquote(const Cell*) v) {
    assert(ANY_PATH_KIND(CELL_HEART(v)));
    if (NOT_CELL_FLAG(v, SEQUENCE_HAS_NODE))
        return false;

    const REBNOD *node1 = VAL_NODE1(v);
    if (NODE_BYTE(node1) & NODE_BYTEMASK_0x01_CELL)
        return false;

    if (SER_FLAVOR(SER(node1)) != FLAVOR_SYMBOL)
        return false;

    return GET_CELL_FLAG(v, REFINEMENT_LIKE);  // !!! Review: test this first?
}

inline static bool IS_REFINEMENT(const Cell *v) {
    assert(ANY_PATH(v));
    return IS_REFINEMENT_CELL(v);
}

inline static bool IS_PREDICATE1_CELL(noquote(const Cell*) v) {
    if (CELL_HEART(v) != REB_TUPLE)
        return false;

    if (NOT_CELL_FLAG(v, SEQUENCE_HAS_NODE))
        return false;

    const REBNOD *node1 = VAL_NODE1(v);
    if (NODE_BYTE(node1) & NODE_BYTEMASK_0x01_CELL)
        return false;

    if (SER_FLAVOR(SER(node1)) != FLAVOR_SYMBOL)
        return false;

    return GET_CELL_FLAG(v, REFINEMENT_LIKE);  // !!! Review: test this first?
}

inline static const Symbol *VAL_PREDICATE1_SYMBOL(
    noquote(const Cell*) v
){
    assert(IS_PREDICATE1_CELL(v));
    return SYM(VAL_NODE1(v));
}

inline static bool IS_PREDICATE(const Cell *v) {
    if (not IS_TUPLE(v))
        return false;

    DECLARE_LOCAL (temp);
    return IS_BLANK(VAL_SEQUENCE_AT(temp, v, 0));
}

inline static const Symbol *VAL_REFINEMENT_SYMBOL(
    noquote(const Cell*) v
){
    assert(IS_REFINEMENT_CELL(v));
    return SYM(VAL_NODE1(v));
}
