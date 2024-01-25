//
//  File: %cell-sequence.h
//  Summary: "Common Definitions for Immutable Interstitially-Delimited Lists"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2023 Ren-C Open Source Contributors
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
// A "Sequence" is a constrained type of array, with elements separated by
// interstitial delimiters.  The two basic forms are PATH! (separated by `/`)
// and TUPLE! (separated by `.`)
//
//     append/dup        ; a 2-element PATH!
//     192.168.0.1       ; a 4-element TUPLE!
//
// Because they are defined by separators *between* elements, sequences of
// zero or one item are not legal.  (This is one reason why they are immutable:
// so the constraint of having at least two items can be validated at the time
// of creation.)
//
// Both forms are allowed to contain WORD!, INTEGER!, GROUP!, BLOCK!, TEXT!,
// QUASI-WORD?!, and TAG!.  There are SET-, GET-, META-, THE-, and TYPE- forms:
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
//     1/2/3/:      ; a 4-element SET-PATH! with BLANK! in the last slot
//     /            ; a 2-element PATH! with BLANK! in the first and last slot
//     a////b       ; a 5-element PATH! with BLANK! in the middle 3 slots
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
//   than a plain WORD!.  And a 2-element TUPLE! like `a.b` bypasses the need
//   to create an Array tracking entity by pointing directly at a managed
//   "pairing" of 2 cells--the same code that is used to compress two INTEGER!
//   into a PAIR!.
//
//   (There are also optimizations for encoding short numeric sequences like IP
//   addresses or colors into single cells...which aren't as important but
//   carried over to preserve history of the feature.)
//
// * Compressed forms detect their compression as follows:
//
//   - Byte compressed forms do not have CELL_FLAG_SEQUENCE_HAS_NODE
//
//   - Pair compression has the first node with NODE_FLAG_CELL
//
//   - Single WORD! forms have the first node as FLAVOR_SYMBOL
//        If CELL_FLAG_REFINEMENT_LIKE it is either a `/foo` or `.foo` case
//        Without the flag, it is either a `foo/` or `foo.` case
//
//   - Uncompressed forms have the first node as FLAVOR_ARRAY
//

INLINE bool Is_Valid_Sequence_Element(
    enum Reb_Kind sequence_kind,
    const Cell* v
){
    assert(Any_Sequence_Kind(sequence_kind));

    // Quasi cases are legal, to support e.g. `~/home/Projects/ren-c/README.md`
    //
    enum Reb_Kind k = Is_Quasiform(v) ? Cell_Heart(v) : VAL_TYPE(v);
    if (
        k == REB_BLANK
        or k == REB_INTEGER
        or k == REB_GROUP
        or k == REB_BLOCK
        or k == REB_TEXT
        or k == REB_TAG
        or k == REB_WORD
    ){
        return true;
    }

    if (k == REB_TUPLE)  // PATH! can have TUPLE!, not vice-versa
        return Any_Path_Kind(sequence_kind);

    return false;
}


// The Try_Init_Any_Sequence_XXX variants will return nullptr if any of the
// requested path elements are not valid.  Instead of an initialized sequence,
// the output cell passed in will be either a null (if the data was
// too short) or it will be the first badly-typed value that was problematic.
//
INLINE Context* Error_Bad_Sequence_Init(const REBVAL *v) {
    if (Is_Nulled(v))
        return Error_Sequence_Too_Short_Raw();
    fail (Error_Bad_Sequence_Item_Raw(v));
}


//=//// UNCOMPRESSED ARRAY SEQUENCE FORM //////////////////////////////////=//

#define Try_Init_Any_Sequence_Arraylike(v,k,a) \
    Try_Init_Any_Sequence_At_Arraylike((v), (k), (a), 0)


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
INLINE REBVAL *Init_Any_Sequence_1(Cell* out, enum Reb_Kind kind) {
    if (Any_Path_Kind(kind))
        Init_Word(out, Canon(SLASH_1));
    else {
        assert(Any_Tuple_Kind(kind));
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

INLINE REBVAL *Try_Leading_Blank_Pathify(
    REBVAL *v,
    enum Reb_Kind kind
){
    assert(Any_Sequence_Kind(kind));

    if (Is_Blank(v))
        return Init_Any_Sequence_1(v, kind);

    if (not Is_Valid_Sequence_Element(kind, v))
        return nullptr;  // leave element in v to indicate "the bad element"

    // See notes at top of file regarding optimizing `/a` into a single cell.
    //
    enum Reb_Kind inner_kind = VAL_TYPE(v);
    if (inner_kind == REB_WORD) {
        Set_Cell_Flag(v, REFINEMENT_LIKE);
        HEART_BYTE(v) = kind;
        return v;
    }

    Cell* p = Alloc_Pairing(NODE_FLAG_MANAGED);
    Init_Blank(p);
    Copy_Cell(Pairing_Second(p), v);

    Init_Pair(v, p);
    HEART_BYTE(v) = kind;

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

INLINE REBVAL *Init_Any_Sequence_Bytes(
    Cell* out,
    enum Reb_Kind kind,
    const Byte* data,
    Size size
){
    Reset_Unquoted_Header_Untracked(
        out,
        FLAG_HEART_BYTE(kind) | CELL_MASK_NO_NODES
    );
    BINDING(out) = nullptr;  // paths are bindable, can't have garbage

    if (size > sizeof(PAYLOAD(Bytes, out).at_least_8) - 1) {  // too big
        Array* a = Make_Array_Core(size, NODE_FLAG_MANAGED);
        for (; size > 0; --size, ++data)
            Init_Integer(Alloc_Tail_Array(a), *data);

        Init_Block(out, Freeze_Array_Shallow(a));  // !!! TBD: compact BINARY!
    }
    else {
        PAYLOAD(Bytes, out).at_least_8[IDX_SEQUENCE_USED] = size;
        Byte* dest = PAYLOAD(Bytes, out).at_least_8 + 1;
        for (; size > 0; --size, ++data, ++dest)
            *dest = *data;
    }

    return cast(REBVAL*, out);
}

#define Init_Tuple_Bytes(out,data,len) \
    Init_Any_Sequence_Bytes((out), REB_TUPLE, (data), (len));

INLINE REBVAL *Try_Init_Any_Sequence_All_Integers(
    Cell* out,
    enum Reb_Kind kind,
    const Cell* head,  // NOTE: Can't use PUSH() or evaluation
    REBLEN len
){
    if (len > sizeof(PAYLOAD(Bytes, out)).at_least_8 - 1)
        return nullptr;  // no optimization yet if won't fit in payload bytes

    if (len < 2)
        return nullptr;

    Reset_Unquoted_Header_Untracked(
        out,
        FLAG_HEART_BYTE(kind) | CELL_MASK_NO_NODES
    );
    BINDING(out) = nullptr;  // paths are bindable, can't be garbage

    PAYLOAD(Bytes, out).at_least_8[IDX_SEQUENCE_USED] = len;

    Byte* bp = PAYLOAD(Bytes, out).at_least_8 + 1;

    const Cell* item = head;
    REBLEN n;
    for (n = 0; n < len; ++n, ++item, ++bp) {
        if (not Is_Integer(item))
            return nullptr;
        REBI64 i64 = VAL_INT64(item);
        if (i64 < 0 or i64 > 255)
            return nullptr;  // only packing byte form for now
        *bp = cast(Byte, i64);
    }

    return cast(REBVAL*, out);
}


//=//// 2-Element "PAIR" SEQUENCE OPTIMIZATION ////////////////////////////=//

INLINE REBVAL *Try_Init_Any_Sequence_Pairlike(
    Sink(Value(*)) out,
    enum Reb_Kind kind,
    const Cell* v1,
    const Cell* v2
){
    if (Is_Blank(v1))
        return Try_Leading_Blank_Pathify(
            SPECIFIC(Copy_Relative_internal(out, v2)),
            kind
        );

    if (not Is_Valid_Sequence_Element(kind, v1)) {
        Copy_Relative_internal(out, v1);
        return nullptr;
    }

    // See notes at top of file regarding optimizing `/a` and `.a`
    //
    enum Reb_Kind inner = VAL_TYPE(v1);
    if (Is_Blank(v2) and inner == REB_WORD) {
        Copy_Relative_internal(out, v1);
        HEART_BYTE(out) = kind;
        return cast(REBVAL*, out);
    }

    if (Is_Integer(v1) and Is_Integer(v2)) {
        Byte buf[2];
        REBI64 i1 = VAL_INT64(v1);
        REBI64 i2 = VAL_INT64(v2);
        if (i1 >= 0 and i2 >= 0 and i1 <= 255 and i2 <= 255) {
            buf[0] = cast(Byte, i1);
            buf[1] = cast(Byte, i2);
            return Init_Any_Sequence_Bytes(out, kind, buf, 2);
        }

        // fall through
    }

    if (not Is_Valid_Sequence_Element(kind, v2)) {
        Copy_Relative_internal(out, v2);
        return nullptr;
    }

    Cell* pairing = Alloc_Pairing(NODE_FLAG_MANAGED);
    Copy_Relative_internal(pairing, v1);
    Copy_Relative_internal(Pairing_Second(pairing), v2);
    Init_Pair(out, pairing);
    HEART_BYTE(out) = kind;

    return cast(REBVAL*, out);
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
INLINE Value(*) Try_Pop_Sequence_Or_Element_Or_Nulled(
    Sink(Value(*)) out,  // the error-triggering value if nullptr returned
    enum Reb_Kind kind,
    StackIndex base
){
    if (TOP_INDEX == base)
        return Init_Nulled(out);

    if (TOP_INDEX - 1 == base) {  // only one item, use as-is if possible
        if (not Is_Valid_Sequence_Element(kind, TOP))
            return nullptr;

        Copy_Cell(out, TOP);
        DROP();

        if (kind != REB_PATH) {  // carry over : or ^ decoration (if possible)
            if (
                not Is_Word(out)
                and not Is_Block(out)
                and not Is_Group(out)
                and not Is_Block(out)
                and not Is_Tuple(out)  // !!! TBD, will support decoration
            ){
                // !!! `out` is reported as the erroring element for why the
                // path is invalid, but this would be valid in a path if we
                // weren't decorating it...rethink how to error on this.
                //
                return nullptr;
            }

            if (kind == REB_SET_PATH)
                Setify(out);
            else if (kind == REB_GET_PATH)
                Getify(out);
            else if (kind == REB_META_PATH)
                Metafy(out);
        }

        return out;  // valid path element, standing alone
    }

    if (TOP_INDEX - base == 2) {  // two-element path optimization
        if (not Try_Init_Any_Sequence_Pairlike(out, kind, TOP - 1, TOP)) {
            Drop_Data_Stack_To(base);
            return nullptr;
        }

        Drop_Data_Stack_To(base);
        return out;
    }

    // Attempt optimization for all-INTEGER! tuple or path, e.g. IP addresses
    // (192.0.0.1) or RGBA color constants 255.0.255.  If optimization fails,
    // use normal array.
    //
    if (Try_Init_Any_Sequence_All_Integers(
        out,
        kind,
        Data_Stack_At(base) + 1,
        TOP_INDEX - base
    )){
        Drop_Data_Stack_To(base);
        return out;
    }

    Array* a = Pop_Stack_Values_Core(base, NODE_FLAG_MANAGED);
    Freeze_Array_Shallow(a);
    if (not Try_Init_Any_Sequence_Arraylike(out, kind, a))
        return nullptr;

    return out;
}


// Note that paths can be initialized with an array, which they will then
// take as immutable...or you can create a `/foo`-style path in a more
// optimized fashion using Refinify()

INLINE Length Cell_Sequence_Len(NoQuote(const Cell*) sequence) {
    assert(Any_Sequence_Kind(Cell_Heart(sequence)));

    if (Not_Cell_Flag(sequence, SEQUENCE_HAS_NODE)) {  // compressed bytes
        assert(Not_Cell_Flag(sequence, SECOND_IS_NODE));
        return PAYLOAD(Bytes, sequence).at_least_8[IDX_SEQUENCE_USED];
    }

    const Node* node1 = Cell_Node1(sequence);
    if (Is_Node_A_Cell(node1))  // see if it's a pairing
        return 2;  // compressed 2-element sequence, sizeof(Stub)

    switch (Series_Flavor(c_cast(Series*, node1))) {
      case FLAVOR_SYMBOL :  // compressed single WORD! sequence
        return 2;

      case FLAVOR_ARRAY : {  // uncompressed sequence
        const Array* a = c_cast(Array*, Cell_Node1(sequence));
        assert(Array_Len(a) >= 2);
        assert(Is_Array_Frozen_Shallow(a));
        return Array_Len(a); }

      default :
        assert(false);
        DEAD_END;
    }
}

// Paths may not always be implemented as arrays, so this mechanism needs to
// be used to read the pointers.  If the value is not in an array, it may
// need to be written to a passed-in storage location.
//
// 1. It's important that the return result from this routine be a Cell* and
//    not a REBVAL*, because path ATs are relative values.  Hence the
//    seemingly minor optimization of not copying out array cells is more than
//    just that...it also assures that the caller isn't passing in a REBVAL*
//    then using it as if it were fully specified.  It serves two purposes.
//
// 2. Because the cell is being viewed as a PATH! or TUPLE!, we cannot view
//    it as a WORD! unless we fiddle the bits at a new location.  The cell
//    is relative and may be at a quote level.
//
// 3. The quotes must be removed because the quotes are intended to be "on"
//    the path or tuple.  If implemented as a pseudo-WORD!
//
INLINE const Cell* Cell_Sequence_At(
    Cell* store,  // relative value, return may not point at this cell [1]
    NoQuote(const Cell*) sequence,
    REBLEN n
){
    assert(store != sequence);
    assert(Any_Sequence_Kind(Cell_Heart(sequence)));

    if (Not_Cell_Flag(sequence, SEQUENCE_HAS_NODE)) {  // compressed bytes
        assert(n < PAYLOAD(Bytes, sequence).at_least_8[IDX_SEQUENCE_USED]);
        return Init_Integer(store, PAYLOAD(Bytes, sequence).at_least_8[n + 1]);
    }

    const Node* node1 = Cell_Node1(sequence);
    if (Is_Node_A_Cell(node1)) {  // test if it's a pairing
        const Cell* pairing = c_cast(Cell*, node1);  // 2 elements compressed
        if (n == 0)
            return pairing;
        assert(n == 1);
        return Pairing_Second(pairing);
    }

    switch (Series_Flavor(x_cast(Series*, node1))) {
      case FLAVOR_SYMBOL : {  // compressed single WORD! sequence
        assert(n < 2);
        if (Get_Cell_Flag(sequence, REFINEMENT_LIKE) ? n == 0 : n != 0)
            return Lib(BLANK);

        Copy_Relative_internal(store, x_cast(const Cell*, sequence));  // [2]
        HEART_BYTE(store) = REB_WORD;
        QUOTE_BYTE(store) = NOQUOTE_1;  // [3]
        return store; }

      case FLAVOR_ARRAY : {  // uncompressed sequence
        const Array* a = c_cast(Array*, Cell_Node1(sequence));
        assert(Array_Len(a) >= 2);
        assert(Is_Array_Frozen_Shallow(a));
        return Array_At(a, n); }  // array is read only

      default :
        assert(false);
        DEAD_END;
    }
}

INLINE Value(*) Copy_Sequence_At(
    Sink(Value(*)) out,
    NoQuote(const Cell*) sequence,
    Specifier* specifier,
    REBLEN n
){
    DECLARE_STABLE (store);
    const Cell* at = Cell_Sequence_At(store, sequence, n);
    return Derelativize(out, at, specifier);
}

INLINE Byte Cell_Sequence_Byte_At(
    NoQuote(const Cell*) sequence,
    REBLEN n
){
    DECLARE_LOCAL (temp);
    const Cell* at = Cell_Sequence_At(temp, sequence, n);
    if (not Is_Integer(at))
        fail ("Cell_Sequence_Byte_At() used on non-byte ANY-SEQUENCE!");
    return VAL_UINT8(at);  // !!! All callers of this routine need vetting
}

INLINE Specifier* Cell_Sequence_Specifier(
    NoQuote(const Cell*) sequence
){
    assert(Any_Sequence_Kind(Cell_Heart(sequence)));

    // Getting the specifier for any of the optimized types means getting
    // the specifier for *that item in the sequence*; the sequence itself
    // does not provide a layer of communication connecting the interior
    // to a frame instance (because there is no actual layer).

    if (Not_Cell_Flag(sequence, SEQUENCE_HAS_NODE))  // compressed bytes
        return SPECIFIED;

    const Node* node1 = Cell_Node1(sequence);
    if (Is_Node_A_Cell(node1))  // see if it's a pairing
        return SPECIFIED;  // compressed 2-element sequence

    switch (Series_Flavor(c_cast(Series*, node1))) {
      case FLAVOR_SYMBOL :  // compressed single WORD! sequence
        return SPECIFIED;

      case FLAVOR_ARRAY :  // uncompressed sequence
        return Cell_Specifier(sequence);

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
INLINE bool Did_Get_Sequence_Bytes(
    void* buf,
    const Cell* sequence,
    Size buf_size
){
    Length len = Cell_Sequence_Len(sequence);

    Byte* dp = cast(Byte*, buf);
    Size i;
    DECLARE_LOCAL (temp);
    for (i = 0; i < buf_size; ++i) {
        if (i >= len) {
            dp[i] = 0;
            continue;
        }
        const Cell* at = Cell_Sequence_At(temp, sequence, i);
        if (not Is_Integer(at))
            return false;
        REBI64 i64 = VAL_INT64(at);
        if (i64 < 0 or i64 > 255)
            return false;

        dp[i] = cast(Byte, i64);
    }
    return true;
}

INLINE void Get_Tuple_Bytes(
    void *buf,
    const Cell* tuple,
    Size buf_size
){
    assert(Is_Tuple(tuple));
    if (not Did_Get_Sequence_Bytes(buf, tuple, buf_size))
        fail ("non-INTEGER! found used with Get_Tuple_Bytes()");
}

#define MAX_TUPLE \
    ((sizeof(uint32_t) * 2))  // !!! No longer a "limit", review callsites



//=//// REFINEMENTS AND PREDICATES ////////////////////////////////////////=//

INLINE REBVAL *Refinify(REBVAL *v) {
    bool success = (Try_Leading_Blank_Pathify(v, REB_PATH) != nullptr);
    assert(success);
    UNUSED(success);
    return v;
}

INLINE bool IS_REFINEMENT_CELL(NoQuote(const Cell*) v) {
    assert(Any_Path_Kind(Cell_Heart(v)));
    if (Not_Cell_Flag(v, SEQUENCE_HAS_NODE))
        return false;

    const Node* node1 = Cell_Node1(v);
    if (Is_Node_A_Cell(node1))
        return false;

    if (Series_Flavor(c_cast(Series*, node1)) != FLAVOR_SYMBOL)
        return false;

    return Get_Cell_Flag(v, REFINEMENT_LIKE);  // !!! Review: test this first?
}

INLINE bool Is_Refinement(const Cell* v) {
    assert(Any_Path(v));
    return IS_REFINEMENT_CELL(v);
}

INLINE bool IS_PREDICATE1_CELL(NoQuote(const Cell*) v) {
    if (Cell_Heart(v) != REB_TUPLE)
        return false;

    if (Not_Cell_Flag(v, SEQUENCE_HAS_NODE))
        return false;

    const Node* node1 = Cell_Node1(v);
    if (Is_Node_A_Cell(node1))
        return false;

    if (Series_Flavor(c_cast(Series*, node1)) != FLAVOR_SYMBOL)
        return false;

    return Get_Cell_Flag(v, REFINEMENT_LIKE);  // !!! Review: test this first?
}

INLINE const Symbol* VAL_REFINEMENT_SYMBOL(
    NoQuote(const Cell*) v
){
    assert(IS_REFINEMENT_CELL(v));
    return c_cast(Symbol*, Cell_Node1(v));
}

// !!! Temporary workaround for what was IS_META_PATH() (now not its own type)
//
INLINE bool IS_QUOTED_PATH(const Cell* v) {
    return Cell_Num_Quotes(v) == 1
        and Cell_Heart(v) == REB_PATH;
}
