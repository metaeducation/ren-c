//
//  File: %cell-sequence.h
//  Summary: "Common Definitions for Immutable Interstitially-Delimited Lists"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2024 Ren-C Open Source Contributors
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
// A "Sequence" is a constrained "arraylike" type with elements separated by
// interstitial delimiters.  Three basic forms are TUPLE! (separated by `.`),
// CHAIN! (separated by ':'), and PATH! (separated by `/`)
//
//     192.168.0.1       ; a 4-element TUPLE!
//     append:dup:part   ; a 3-element CHAIN!
//     lib/insert        ; a 2-element PATH!
//
// Because they are defined by separators *between* elements, sequences of
// zero or one item are not legal.  (This is one reason why they are immutable:
// so the constraint of having at least two items can be validated at the time
// of creation.)
//
// Both forms are allowed to contain WORD!, INTEGER!, GROUP!, BLOCK!, TEXT!,
// and TAG!.  Quasiforms of these types (where legal) are also permitted.
// There are SET-, GET-, META-, THE-, and TYPE- forms:
//
//     <abc>/(d e f)/[g h i]    ; a 3-element PATH!
//     foo.1.bar                ; a 3-element TUPLE!
//     ^abc.(def)               ; a 2-element META-TUPLE!
//     @<a>/<b>/<c>             ; a 3-element THE-TUPLE!
//     ~/home/README            ; a 3-element PATH!
//
// PATH!s may contain TUPLE!s, but not vice versa.  This means that mixed
// usage can be interpreted unambiguously:
//
//     a.b.c/d.e.f    ; a 2-element PATH! containing 3-element TUPLEs
//     a/b/c.d/e/f    ; a 5-element PATH! with 2-element TUPLE! in the middle
//
// It is also legal to put BLANK! in slots at the head or tail.  They render
// invisibly, allowing you to begin or terminate sequences with the delimiter:
//
//     .foo.bar     ; a 3-element TUPLE! with BLANK! in the first slot
//     1/2/3/       ; a 4-element PATH! with BLANK! in the last slot
//     /a           ; a 2-element PATH! with BLANK! in the first slot
//
// Internal blanks are not allowed.  Because although there might be some
// theoretical use for `a//b` or similar sequences, those aren't as compelling
// as being able to have `http://example.com` be URL! (vs. a 3-element PATH!)
//
// Neither PATH! nor TUPLE may contain "arrow-words" in any slot (those with
// `>` or `<` in their spelling), so interpretation of TAG!s is unambiguous:
//
//     .<.>.     ; a 3-element TUPLE! with TAG! <.> in slot 2
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * Reduced cases such as "2-element path" `/` and the "2-element tuple" `.`
//   are instead chosen as WORD!.  This was considered non-negotiable, that
//   `/` be allowed to mean divide.  Making it a PATH! that divided turned
//   out to be much more convoluted than having special word flags.  (See
//   SYMBOL_FLAG_ILLEGAL_IN_ANY_SEQUENCE etc. for how these are handled,
//   where `.` can be put in paths but `/` can't appear in any path or tuple.)
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


// 1. Quasiforms are legal in paths--which is one of the reasons why paths
//    themselves aren't allowed to be quasiforms.  Because `~/foo/~` is more
//    useful as a 3-element path with quasiform blanks in the first and last
//    positions, than a quasiform path is useful.
//
//    (Note that exceptions like [~/~ ~//~ ~...~] are quasi-words.)
//
INLINE Option(Error*) Trap_Check_Sequence_Element(
    Heart sequence_heart,
    const Element* e
){
    assert(Any_Sequence_Kind(sequence_heart));

    if (Is_Quoted(e))  // allow quasiforms, but not quoteds [1]
        return Error_Bad_Sequence_Item_Raw(e);

    Heart h = Cell_Heart(e);
    if (
        h == REB_INTEGER
        or h == REB_GROUP
        or h == REB_BLOCK
        or h == REB_TEXT
        or h == REB_TAG
    ){
        return nullptr;
    }

    if (h == REB_BLANK) {
        if (QUOTE_BYTE(e) == QUASIFORM_2)  // ~ is quasiform blank (trash)
            return nullptr;  // Legal, e.g. `~/home/Projects/ren-c/README.md`

        return Error_Bad_Sequence_Blank_Raw();  // blank only legal at head
    }

    if (h == REB_WORD) {
        const Symbol* symbol = Cell_Word_Symbol(e);
        if (
            sequence_heart != REB_CHAIN  // !!! temporary for //: -- review
            and Get_Subclass_Flag(SYMBOL, symbol, ILLEGAL_IN_ANY_SEQUENCE)
        ){
            return Error_Bad_Sequence_Item_Raw(e);  //  [<| |>] => <|/|>  ; tag
        }
        if (Any_Path_Kind(sequence_heart))
            return nullptr;
        if (Get_Subclass_Flag(SYMBOL, symbol, ILLEGAL_IN_ANY_TUPLE))
            return Error_Bad_Sequence_Item_Raw(e);  // e.g. contains a slash
        return nullptr;  // all other words should be okay
    }

    if (h == REB_PATH)  // inserting path, can't put in path, tuple, or chain
        return Error_Bad_Sequence_Item_Raw(e);

    if (h == REB_CHAIN) {  // inserting a chain
        if (sequence_heart != REB_CHAIN and sequence_heart != REB_TUPLE)
            return nullptr;  // legal in any sequence that isn't tuple/chain
        return Error_Bad_Sequence_Item_Raw(e);
    }

    if (h == REB_TUPLE) {  // inserting a tuple
        if (sequence_heart != REB_TUPLE)
            return nullptr;  // legal in any sequence that isn't tuple
        return Error_Bad_Sequence_Item_Raw(e);
    }

    return Error_Bad_Sequence_Item_Raw(e);
}


//=//// UNCOMPRESSED ARRAY SEQUENCE FORM //////////////////////////////////=//

#define Trap_Init_Any_Sequence_Listlike(out,heart,a) \
    Trap_Init_Any_Sequence_At_Listlike((out), (heart), (a), 0)


//=//// Leading-BLANK! SEQUENCE OPTIMIZATION //////////////////////////////=//
//
// Ren-C has no REFINEMENT! datatype, so `/foo` is a PATH!, which generalizes
// to where `/foo/bar` is a PATH! as well, etc.
//
// In order to make this not cost more than a "REFINEMENT!" word type did in
// R3-Alpha, the underlying representation of `/foo` in the cell is the same
// as an ANY-WORD?
//
// 1. !!! Temporarily raise attention to usage like `.5` or `5.` to guide
//    people that these are contentious with tuples.  There is no other way
//    to represent such tuples--while DECIMAL! has an alternative by
//    including the zero.  This doesn't put any decision in stone, but
//    reserves the right to make a decision at a later time.
//
INLINE Option(Error*) Trap_Blank_Head_Or_Tail_Sequencify(
    Element* e,
    Heart heart,
    Flags flag
){
    assert(flag == CELL_MASK_0 or flag == CELL_FLAG_REFINEMENT_LIKE);
    assert(Any_Sequence_Kind(heart));

    Option(Error*) error = Trap_Check_Sequence_Element(heart, e);
    if (error)
        return error;

    if (Is_Word(e)) {  // see notes at top of file on `/a` cell optimization
        e->header.bits |= flag;
        HEART_BYTE(e) = heart;  // e.g. REB_WORD => REB_PATH
        return nullptr;
    }

    if (Any_List(e)) {  // try mirror optimization
        assert(Is_Group(e) or Is_Block(e));  // only valid kinds
        const Array* a = Cell_Array(e);
        if (MIRROR_BYTE(a) == REB_0 or MIRROR_BYTE(a) == Cell_Heart(e)) {
            MIRROR_BYTE(a) = Cell_Heart(e);  // remember what kind it is
            HEART_BYTE(e) = heart;  // e.g. REB_BLOCK => REB_PATH
            e->header.bits |= flag;
            return nullptr;
        }
    }

    if (Is_Integer(e)) {
        if (heart == REB_TUPLE) {
            return Error_User(  // reserve notation for future use [1]
                "5. and .5 currently reserved, please use 5.0 and 0.5"
            );
        }
        // fallthrough (should be able to single cell optimize any INTEGER!)
    }

    Pairing* p = Alloc_Pairing(NODE_FLAG_MANAGED);
    if (flag == CELL_FLAG_REFINEMENT_LIKE) {
        Init_Blank(Pairing_First(p));
        Copy_Cell(Pairing_Second(p), e);
    }
    else {
        Copy_Cell(Pairing_First(p), e);
        Init_Blank(Pairing_Second(p));
    }

    Reset_Cell_Header_Untracked(
        e,
        CELL_FLAG_FIRST_IS_NODE | FLAG_HEART_BYTE(heart)
    );
    Tweak_Cell_Pairing(e, p);
  #ifdef ZERO_UNUSED_CELL_FIELDS
    PAYLOAD(Any, e).second.corrupt = CORRUPTZERO;  // payload second not used
  #endif
    BINDING(e) = UNBOUND;  // "arraylike", needs binding
    return nullptr;
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

INLINE Element* Init_Any_Sequence_Bytes(
    Sink(Element*) out,
    Heart heart,
    const Byte* data,
    Size size
){
    assert(Any_Sequence_Kind(heart));
    Reset_Cell_Header_Untracked(
        out,
        FLAG_HEART_BYTE(heart) | CELL_MASK_NO_NODES
    );
    BINDING(out) = nullptr;  // paths are bindable, can't have garbage

    if (size > Size_Of(PAYLOAD(Bytes, out).at_least_8) - 1) {  // too big
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

    return out;
}

#define Init_Tuple_Bytes(out,data,len) \
    Init_Any_Sequence_Bytes((out), REB_TUPLE, (data), (len));

INLINE Option(Element*) Try_Init_Any_Sequence_All_Integers(
    Sink(Element*) out,
    Heart heart,
    const Value* head,  // NOTE: Can't use PUSH() or evaluation
    REBLEN len
){
    assert(Any_Sequence_Kind(heart));

    if (len > Size_Of(PAYLOAD(Bytes, out).at_least_8) - 1)
        return nullptr;  // no optimization yet if won't fit in payload bytes

    if (len < 2)
        return nullptr;

    Reset_Cell_Header_Untracked(
        out,
        FLAG_HEART_BYTE(heart) | CELL_MASK_NO_NODES
    );
    BINDING(out) = nullptr;  // paths are bindable, can't be garbage

    PAYLOAD(Bytes, out).at_least_8[IDX_SEQUENCE_USED] = len;

    Byte* bp = PAYLOAD(Bytes, out).at_least_8 + 1;

    const Value* item = head;
    REBLEN n;
    for (n = 0; n < len; ++n, ++item, ++bp) {
        if (not Is_Integer(item))
            return nullptr;
        REBI64 i64 = VAL_INT64(item);
        if (i64 < 0 or i64 > 255)
            return nullptr;  // only packing byte form for now
        *bp = cast(Byte, i64);
    }

    return out;
}


//=//// 2-Element "PAIR" SEQUENCE OPTIMIZATION ////////////////////////////=//

INLINE Option(Error*) Trap_Init_Any_Sequence_Or_Conflation_Pairlike(
    Sink(Element*) out,
    Heart heart,
    const Element* first,
    const Element* second
){
    assert(Any_Sequence_Kind(heart));

    if (
        (Is_Trash(first) and Is_Trash(second))  // ~/~ is a WORD!
        or (Is_Blank(first) and Is_Blank(second))  // plain / is a WORD!
    ){
        if (Any_Path_Kind(heart))
            Init_Word(out, Canon(SLASH_1));
        else {
            assert(Any_Tuple_Kind(heart));
            Init_Word(out, Canon(DOT_1));
        }
        if (Is_Trash(first))
            Quasify(out);
        return nullptr;
    }

    if (Is_Blank(first)) {  // try optimize e.g. `/a` or `.a` or `:a` etc.
        Copy_Cell(out, second);
        return Trap_Blank_Head_Or_Tail_Sequencify(
            out, heart, CELL_FLAG_REFINEMENT_LIKE
        );
    }
    else if (Is_Blank(second)) {
        Copy_Cell(out, first);
        return Trap_Blank_Head_Or_Tail_Sequencify(  // optimize `a/` or `a:`
            out, heart, CELL_MASK_0
        );
    }

    if (Is_Integer(first) and Is_Integer(second)) {
        Byte buf[2];
        REBI64 i1 = VAL_INT64(first);
        REBI64 i2 = VAL_INT64(second);
        if (i1 >= 0 and i2 >= 0 and i1 <= 255 and i2 <= 255) {
            buf[0] = cast(Byte, i1);
            buf[1] = cast(Byte, i2);
            Init_Any_Sequence_Bytes(out, heart, buf, 2);
            return nullptr;
        }

        // fall through
    }

    Option(Error*) err1 = Trap_Check_Sequence_Element(heart, first);
    if (err1)
        return err1;

    Option(Error*) err2 = Trap_Check_Sequence_Element(heart, second);
    if (err2)
        return err2;

    Pairing* pairing = Alloc_Pairing(NODE_FLAG_MANAGED);
    Copy_Cell(Pairing_First(pairing), first);
    Copy_Cell(Pairing_Second(pairing), second);

    Reset_Cell_Header_Untracked(
        out,
        CELL_FLAG_FIRST_IS_NODE | FLAG_HEART_BYTE(heart)
    );
    Tweak_Cell_Pairing(out, pairing);
  #ifdef ZERO_UNUSED_CELL_FIELDS
    PAYLOAD(Any, out).second.corrupt = CORRUPTZERO;  // payload second not used
  #endif
    BINDING(out) = UNBOUND;  // "arraylike", needs binding
    return nullptr;
}


INLINE Option(Error*) Trap_Init_Any_Sequence_Pairlike(
    Sink(Element*) out,
    Heart heart,
    const Element* first,
    const Element* second
){
    Option(Error*) error = Trap_Init_Any_Sequence_Or_Conflation_Pairlike(
        out, heart, first, second
    );
    if (error)
        return error;

    if (not Any_Sequence(out))
        return Error_Conflated_Sequence_Raw(out);

    return nullptr;
}

INLINE Option(Error*) Trap_Pop_Sequence_Or_Conflation(
    Sink(Element*) out,
    Heart heart,
    StackIndex base
){
    if (TOP_INDEX - base < 2) {
        Drop_Data_Stack_To(base);
        return Error_Sequence_Too_Short_Raw();
    }

    if (TOP_INDEX - base == 2) {  // two-element path optimization
        assert(not Is_Antiform(TOP - 1));
        assert(not Is_Antiform(TOP));
        Option(Error*) trap = Trap_Init_Any_Sequence_Or_Conflation_Pairlike(
            out,
            heart,
            cast(Element*, TOP - 1),
            cast(Element*, TOP)
        );
        Drop_Data_Stack_To(base);
        return trap;
    }

    if (Try_Init_Any_Sequence_All_Integers(  // optimize e.g. 192.0.0.1
        out,
        heart,
        Data_Stack_At(base) + 1,
        TOP_INDEX - base
    )){
        Drop_Data_Stack_To(base);  // optimization worked! drop stack...
        return nullptr;
    }

    assert(TOP_INDEX - base > 2);  // guaranteed from above
    Array* a = Pop_Stack_Values_Core(base, NODE_FLAG_MANAGED);
    Freeze_Array_Shallow(a);
    return Trap_Init_Any_Sequence_Listlike(out, heart, a);
}

INLINE Option(Error*) Trap_Pop_Sequence(
    Sink(Element*) out,
    Heart heart,
    StackIndex base
){
    Option(Error*) error = Trap_Pop_Sequence_Or_Conflation(out, heart, base);
    if (error)
        return error;

    if (not Any_Sequence(out))
        return Error_Conflated_Sequence_Raw(out);

    return nullptr;
}


// This is a general utility for turning stack values into something that is
// either pathlike or value like.  It is used in COMPOSE of paths, which
// allows things like:
//
//     >> compose (void)/a
//     == a
//
//     >> compose (blank)/a
//     == /a
//
//     >> compose (void)/(void)/(void)
//     == ~null~  ; anti
//
// While you can't create a PATH! or TUPLE! out of just blanks, this function
// will decay two blanks in a path to the WORD! `/` and two blanks in a tuple
// to the WORD! '.' -- this could be extended to allow more blanks to get words
// like `///` if that were deemed interesting.
//
INLINE Option(Error*) Trap_Pop_Sequence_Or_Element_Or_Nulled(
    Sink(Value*) out,
    Heart sequence_heart,
    StackIndex base
){
    if (TOP_INDEX == base) {  // nothing to pop
        Init_Nulled(out);
        return nullptr;
    }

    if (TOP_INDEX - 1 == base) {  // only one item, use as-is if possible
        Move_Drop_Top_Stack_Element(out);  // balances stack, ensures element

        if (not Is_Blank(out)) {  // allow _.(void) to be _ if COMPOSE'd
            Option(Error*) error = Trap_Check_Sequence_Element(
                sequence_heart,
                cast(Element*, out)
            );
            if (error)
                return error;
        }

        Sigil sigil = maybe Sigil_Of_Kind(sequence_heart);
        if (not sigil)  // just wanted a plain pa/th or tu.p.le
            return nullptr;  // let the item just decay to itself as-is

        if (not Any_Plain_Value(out))
            return Error_Cant_Decorate_Type_Raw(out);

        if (
            not Is_Word(out)
            and not Is_Block(out)
            and not Is_Group(out)
            and not Is_Block(out)
            and not Is_Tuple(out)
        ){
            return Error_Cant_Decorate_Type_Raw(out);
        }

        HEART_BYTE(out) = Sigilize_Any_Plain_Kind(sigil, Cell_Heart(out));
        return nullptr;  // pathness or tupleness vanished, just the value
    }

    return Trap_Pop_Sequence_Or_Conflation(out, sequence_heart, base);
}


// Note that paths can be initialized with an array, which they will then
// take as immutable...or you can create a `/foo`-style path in a more
// optimized fashion using Refinify()

INLINE Length Cell_Sequence_Len(const Cell* c) {
    assert(Any_Sequence_Kind(Cell_Heart(c)));

    if (Not_Cell_Flag(c, SEQUENCE_HAS_NODE)) {  // compressed bytes
        assert(Not_Cell_Flag(c, SECOND_IS_NODE));
        return PAYLOAD(Bytes, c).at_least_8[IDX_SEQUENCE_USED];
    }

    const Node* node1 = Cell_Node1(c);
    if (Is_Node_A_Cell(node1))  // see if it's a pairing
        return 2;  // compressed 2-element sequence, sizeof(Stub)

    switch (Stub_Flavor(c_cast(Flex*, node1))) {
      case FLAVOR_SYMBOL :  // compressed single WORD! sequence
        return 2;

      case FLAVOR_ARRAY : {  // uncompressed sequence
        const Array* a = c_cast(Array*, node1);
        if (MIRROR_BYTE(a) != REB_0)
            return 2;  // e.g. `(a):` stores REB_GROUP in the mirror byte
        assert(Array_Len(a) >= 2);
        assert(Is_Array_Frozen_Shallow(a));
        return Array_Len(a); }

      default :
        assert(false);
        DEAD_END;
    }
}

// Paths may not always be implemented as arrays, so this mechanism needs to
// be used to read the pointers.  Writes to the passed in location.
//
// 1. It would sometimes be possible to return a pointer into the array and
//    not copy a cell.  But
//
// 2. Because the cell is being viewed as a PATH! or TUPLE!, we cannot view
//    it as a WORD! unless we fiddle the bits at a new location.  The cell
//    is relative and may be at a quote level.
//
// 3. The quotes must be removed because the quotes are intended to be "on"
//    the path or tuple.  If implemented as a pseudo-WORD!
//
// 4. "Mirror Bytes" are the idea that things like a GROUP! or a BLOCK! which
//    are put into a sequence already have a node allocated, and that array
//    node has a place where size is written for non-array types when using
//    the small series optimization.
//
INLINE Element* Derelativize_Sequence_At(
    Sink(Element*) out,
    const Element* sequence,
    Context* context,
    REBLEN n
){
    assert(out != sequence);
    assert(Any_Sequence_Kind(Cell_Heart(sequence)));  // !!! should not be cell

    if (Not_Cell_Flag(sequence, SEQUENCE_HAS_NODE)) {  // compressed bytes
        assert(n < PAYLOAD(Bytes, sequence).at_least_8[IDX_SEQUENCE_USED]);
        return Init_Integer(out, PAYLOAD(Bytes, sequence).at_least_8[n + 1]);
    }

    const Node* node1 = Cell_Node1(sequence);
    if (Is_Node_A_Cell(node1)) {  // test if it's a pairing
        const Pairing* p = c_cast(Pairing*, node1);  // compressed pair
        if (n == 0)
            return Derelativize(out, Pairing_First(p), context);
        assert(n == 1);
        return Derelativize(out, Pairing_Second(p), context);
    }

    switch (Stub_Flavor(x_cast(Flex*, node1))) {
      case FLAVOR_SYMBOL : {  // compressed single WORD! sequence
        assert(n < 2);
        if (Get_Cell_Flag(sequence, REFINEMENT_LIKE) ? n == 0 : n != 0)
            return Init_Blank(out);

        Derelativize(out, sequence, context);  // [2]
        HEART_BYTE(out) = REB_WORD;
        QUOTE_BYTE(out) = NOQUOTE_1;  // [3]
        return out; }

      case FLAVOR_ARRAY : {  // uncompressed sequence, or compressed "mirror"
        const Array* a = c_cast(Array*, Cell_Node1(sequence));
        if (MIRROR_BYTE(a) != REB_0) {  // [4]
            assert(n < 2);
            if (Get_Cell_Flag(sequence, REFINEMENT_LIKE) ? n == 0 : n != 0)
                return Init_Blank(out);

            Derelativize(out, sequence, context);
            HEART_BYTE(out) = MIRROR_BYTE(a);
            QUOTE_BYTE(out) = NOQUOTE_1;  // [3]
            return out;
        }
        assert(Array_Len(a) >= 2);
        assert(Is_Array_Frozen_Shallow(a));
        return Derelativize(out, Array_At(a, n), context); }

      default :
        assert(false);
        DEAD_END;
    }
}

// !!! Cell-based routines ignore quotes, and want to be able to see the
// items in a sequence.  We hackily cast the cell to an element, and the
// Derelativize routine checks only the cell heart.  This is backwards:
// derelativizing should be on top of a cell routine.
//
#define Copy_Sequence_At(out,sequence,n) \
    Derelativize_Sequence_At((out), c_cast(Element*, sequence), SPECIFIED, (n))

INLINE Byte Cell_Sequence_Byte_At(const Cell* sequence, REBLEN n) {
    DECLARE_ATOM (at);
    Copy_Sequence_At(at, sequence, n);
    if (not Is_Integer(at))
        fail ("Cell_Sequence_Byte_At() used on non-byte ANY-SEQUENCE?");
    return VAL_UINT8(at);  // !!! All callers of this routine need vetting
}

INLINE Context* Cell_Sequence_Binding(const Cell* sequence) {
    assert(Any_Sequence_Kind(Cell_Heart(sequence)));

    // Getting the binding for any of the optimized types means getting
    // the binding for *that item in the sequence*; the sequence itself
    // does not provide a layer of communication connecting the interior
    // to a frame instance (because there is no actual layer).

    if (Not_Cell_Flag(sequence, SEQUENCE_HAS_NODE))  // compressed bytes
        return SPECIFIED;

    const Node* node1 = Cell_Node1(sequence);
    if (Is_Node_A_Cell(node1))  // see if it's a pairing
        return BINDING(sequence);  // compressed 2-element sequence

    switch (Stub_Flavor(c_cast(Flex*, node1))) {
      case FLAVOR_SYMBOL:  // compressed single WORD! sequence
        return SPECIFIED;

      case FLAVOR_ARRAY: {  // uncompressed sequence
        const Array* a = Cell_Array(sequence);
        if (MIRROR_BYTE(a) != REB_0)
            return SPECIFIED;
        return BINDING(sequence); }

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
INLINE bool Try_Get_Sequence_Bytes(
    void* buf,
    const Cell* sequence,
    Size buf_size
){
    Length len = Cell_Sequence_Len(sequence);

    Byte* dp = cast(Byte*, buf);
    Size i;
    DECLARE_ELEMENT (temp);
    for (i = 0; i < buf_size; ++i) {
        if (i >= len) {
            dp[i] = 0;
            continue;
        }
        Copy_Sequence_At(temp, sequence, i);
        if (not Is_Integer(temp))
            return false;
        REBI64 i64 = VAL_INT64(temp);
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
    assert(Cell_Heart(tuple) == REB_TUPLE);
    if (not Try_Get_Sequence_Bytes(buf, tuple, buf_size))
        fail ("non-INTEGER! found used with Get_Tuple_Bytes()");
}

#define MAX_TUPLE \
    ((Size_Of(uint32_t) * 2))  // !!! No longer a "limit", review callsites



//=//// REFINEMENTS AND PREDICATES ////////////////////////////////////////=//

INLINE Element* Refinify(Element* e) {
    Option(Error*) error = Trap_Blank_Head_Or_Tail_Sequencify(
        e, REB_PATH, CELL_FLAG_REFINEMENT_LIKE
    );
    if (error)
        fail (unwrap error);
    return e;
}

INLINE bool IS_REFINEMENT_CELL(const Cell* v) {
    assert(Any_Path_Kind(Cell_Heart(v)));
    if (Not_Cell_Flag(v, SEQUENCE_HAS_NODE))
        return false;

    const Node* node1 = Cell_Node1(v);
    if (Is_Node_A_Cell(node1))
        return false;

    if (Stub_Flavor(c_cast(Flex*, node1)) != FLAVOR_SYMBOL)
        return false;

    return Get_Cell_Flag(v, REFINEMENT_LIKE);  // !!! Review: test this first?
}

INLINE bool Is_Refinement(const Value* v) {
    assert(Any_Path(v));
    return IS_REFINEMENT_CELL(v);
}

INLINE bool IS_PREDICATE1_CELL(const Cell* v) {
    if (Cell_Heart(v) != REB_TUPLE)
        return false;

    if (Not_Cell_Flag(v, SEQUENCE_HAS_NODE))
        return false;

    const Node* node1 = Cell_Node1(v);
    if (Is_Node_A_Cell(node1))
        return false;

    if (Stub_Flavor(c_cast(Flex*, node1)) != FLAVOR_SYMBOL)
        return false;

    return Get_Cell_Flag(v, REFINEMENT_LIKE);  // !!! Review: test this first?
}

INLINE const Symbol* VAL_REFINEMENT_SYMBOL(const Cell* v) {
    assert(IS_REFINEMENT_CELL(v));
    return c_cast(Symbol*, Cell_Node1(v));
}

// !!! Temporary workaround for what was IS_META_PATH() (now not its own type)
//
INLINE bool IS_QUOTED_PATH(const Cell* v) {
    return Cell_Num_Quotes(v) == 1
        and Cell_Heart(v) == REB_PATH;
}

INLINE Element* Init_Set_Word(Sink(Element*) out, const Symbol* s) {
    Init_Word(out, s);
    HEART_BYTE(out) = REB_CHAIN;
    return out;
}

INLINE Element* Init_Get_Word(Sink(Element*) out, const Symbol* s) {
    Init_Word(out, s);
    HEART_BYTE(out) = REB_CHAIN;
    Set_Cell_Flag(out, REFINEMENT_LIKE);
    return out;
}


INLINE Option(Heart) Try_Get_Sequence_Singleheart(
    Sink(bool*) leading_blank,
    const Cell* c
){
    assert(Any_Sequence_Kind(Cell_Heart(c)));

    if (Not_Cell_Flag(c, SEQUENCE_HAS_NODE))  // compressed bytes
        return REB_0;

    if (Is_Node_A_Cell(Cell_Node1(c))) {
        const Pairing* p = u_cast(const Pairing*, Cell_Node1(c));
        if (Is_Blank(Pairing_First(p))) {
            *leading_blank = true;
            return Cell_Heart(Pairing_Second(p));
        }
        if (Is_Blank(Pairing_Second(p))) {
            *leading_blank = false;
            return Cell_Heart(Pairing_First(p));
        }
        return REB_0;
    }

    const Stub* s = u_cast(const Stub*, Cell_Node1(c));
    if (Stub_Flavor(s) == FLAVOR_SYMBOL) {
        *leading_blank = did (c->header.bits & CELL_FLAG_REFINEMENT_LIKE);
        return REB_WORD;
    }

    Heart h = u_cast(Heart, MIRROR_BYTE(s));
    if (h == REB_0)  // s actually is the sequence elements, not one element
        return REB_0;
    *leading_blank = did (c->header.bits & CELL_FLAG_REFINEMENT_LIKE);
    return h;
}


// GET-WORD! and SET-WORD!

INLINE bool Is_Like_Get_Word(const Cell* c) {
    bool leading_blank;
    return (
        Cell_Heart(c) == REB_CHAIN and
        REB_WORD == Try_Get_Sequence_Singleheart(&leading_blank, c)
        and leading_blank
    );
}

INLINE bool Is_Get_Word(const Value* v)
  { return QUOTE_BYTE(v) == NOQUOTE_1 and Is_Like_Get_Word(v); }

INLINE bool Is_Like_Set_Word(const Cell* c) {
    bool leading_blank;
    return (
        Cell_Heart(c) == REB_CHAIN and
        REB_WORD == Try_Get_Sequence_Singleheart(&leading_blank, c)
        and not leading_blank
    );
}

INLINE bool Is_Set_Word(const Value* v)
  { return QUOTE_BYTE(v) == NOQUOTE_1 and Is_Like_Set_Word(v); }


// GET-TUPLE! and SET-TUPLE!

INLINE bool Is_Like_Get_Tuple(const Cell* c) {
    bool leading_blank;
    return (
        Cell_Heart(c) == REB_CHAIN and
        REB_TUPLE == Try_Get_Sequence_Singleheart(&leading_blank, c)
        and leading_blank
    );
}

INLINE bool Is_Get_Tuple(const Value* v)
  { return QUOTE_BYTE(v) == NOQUOTE_1 and Is_Like_Get_Tuple(v); }

INLINE bool Is_Like_Set_Tuple(const Cell* c) {
    bool leading_blank;
    return (
        Cell_Heart(c) == REB_CHAIN and
        REB_TUPLE == Try_Get_Sequence_Singleheart(&leading_blank, c)
        and not leading_blank
    );
}

INLINE bool Is_Set_Tuple(const Value* v)
  { return QUOTE_BYTE(v) == NOQUOTE_1 and Is_Like_Set_Tuple(v); }


// GET-BLOCK! and SET-BLOCK!

INLINE bool Is_Like_Get_Block(const Cell* c) {
    bool leading_blank;
    return (
        Cell_Heart(c) == REB_CHAIN and
        REB_BLOCK == Try_Get_Sequence_Singleheart(&leading_blank, c)
        and leading_blank
    );
}

INLINE bool Is_Get_Block(const Value* v)
  { return QUOTE_BYTE(v) == NOQUOTE_1 and Is_Like_Get_Block(v); }

INLINE bool Is_Like_Set_Block(const Cell* c) {
    bool leading_blank;
    return (
        Cell_Heart(c) == REB_CHAIN and
        REB_BLOCK == Try_Get_Sequence_Singleheart(&leading_blank, c)
        and not leading_blank
    );
}

INLINE bool Is_Set_Block(const Value* v)
  { return QUOTE_BYTE(v) == NOQUOTE_1 and Is_Like_Set_Block(v); }


// GET-GROUP! and SET-GROUP!

INLINE bool Is_Like_Get_Group(const Cell* c) {
    bool leading_blank;
    return (
        Cell_Heart(c) == REB_CHAIN and
        REB_GROUP == Try_Get_Sequence_Singleheart(&leading_blank, c)
        and leading_blank
    );
}

INLINE bool Is_Get_Group(const Value* v)
  { return QUOTE_BYTE(v) == NOQUOTE_1 and Is_Like_Get_Group(v); }

INLINE bool Is_Like_Set_Group(const Cell* c) {
    bool leading_blank;
    return (
        Cell_Heart(c) == REB_CHAIN and
        REB_GROUP == Try_Get_Sequence_Singleheart(&leading_blank, c)
        and not leading_blank
    );
}

INLINE bool Is_Set_Group(const Value* v)
  { return QUOTE_BYTE(v) == NOQUOTE_1 and Is_Like_Set_Group(v); }


INLINE bool Any_Set_Value(const Value* v) {  // !!! optimize?
    bool leading_blank;
    return (
        QUOTE_BYTE(v) == NOQUOTE_1
        and HEART_BYTE(v) == REB_CHAIN
        and Try_Get_Sequence_Singleheart(&leading_blank, v)
        and not leading_blank
    );
}

INLINE bool Any_Get_Value(const Value* v) {  // !!! optimize?
    bool leading_blank;
    return (
        QUOTE_BYTE(v) == NOQUOTE_1
        and HEART_BYTE(v) == REB_CHAIN
        and Try_Get_Sequence_Singleheart(&leading_blank, v)
        and leading_blank
    );
}
