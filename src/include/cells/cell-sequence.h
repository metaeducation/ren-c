//
//  file: %cell-sequence.h
//  summary: "Common Definitions for Immutable Interstitially-Delimited Lists"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
// There are versions with Sigil as well.
//
//     <abc>/(d e f)/[g h i]    ; a 3-element PATH!
//     foo.1.bar                ; a 3-element TUPLE!
//     ^abc.(def)               ; a 2-element ^TUPLE!
//     @<a>/<b>/<c>             ; a 3-element @TUPLE!
//     ~/home/README            ; a 3-element PATH!
//
// PATH!s may contain TUPLE!s, but not vice versa.  This means that mixed
// usage can be interpreted unambiguously:
//
//     a.b.c/d.e.f    ; a 2-element PATH! containing 3-element TUPLEs
//     a/b/c.d/e/f    ; a 5-element PATH! with 2-element TUPLE! in the middle
//
// It is also legal to put SPACE in slots at the head or tail.  They render
// invisibly, allowing you to begin or terminate sequences with the delimiter:
//
//     .foo.bar     ; a 3-element TUPLE! with SPACE in the first slot
//     1/2/3/       ; a 4-element PATH! with SPACE in the last slot
//     /a           ; a 2-element PATH! with SPACE in the first slot
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
//   - Byte compressed forms have CELL_FLAG_DONT_MARK_PAYLOAD_1, which you can
//     test for more clearly with (not Sequence_Has_Pointer(cell))
//
//   - Pair compression has CELL_PAYLOAD_1() with BASE_FLAG_CELL
//
//   - Single WORD! forms have CELL_PAYLOAD_1() as FLAVOR_SYMBOL
//        If CELL_FLAG_LEADING_SPACE it is either a `/foo` or `.foo` case
//        Without the flag, it is either a `foo/` or `foo.` case
//
//   - Uncompressed forms have CELL_PAYLOAD_1() as FLAVOR_SOURCE
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
    const Element* e,
    bool is_head
){
    assert(Any_Sequence_Type(sequence_heart));

    Option(Heart) h = Heart_Of(e);

    if (is_head) {  // note quasiforms legal, even at head [1]
        if (Is_Quoted(e) or Sigil_Of(e))  // $a.b => $[a b], not [$a b]
            return Error_Bad_Sequence_Head_Raw(e);
    }

    if (h == TYPE_PATH)  // path can't put be put in path, tuple, or chain
        goto bad_sequence_item;

    if (h == TYPE_CHAIN) {  // inserting a chain
        if (sequence_heart == TYPE_PATH)
            return SUCCESS;  // chains can only be put in paths
        goto bad_sequence_item;
    }

    if (h == TYPE_TUPLE) {  // inserting a tuple
        if (sequence_heart != TYPE_TUPLE)
            return SUCCESS;  // legal in non-tuple sequences (path, chain)
        goto bad_sequence_item;
    }

    if (h == TYPE_RUNE) {
        if (Is_Quasar(e))  // Legal, e.g. `~/home/Projects/ren-c/README.md`
            return SUCCESS;

        if (Any_Sigiled_Space(e))
            return SUCCESS;  // single-char forms legal for now

        if (Is_Space(e)) {
            assert(not is_head); // callers should check space at head or tail
            return Error_Bad_Sequence_Space_Raw();  // space only legal at head
        }

        goto bad_sequence_item;
    }

    if (not Any_Sequencable_Type(h))
        goto bad_sequence_item;

    if (h == TYPE_WORD) {
        const Symbol* symbol = Cell_Word_Symbol(e);
        if (symbol == CANON(DOT_1) and sequence_heart != TYPE_TUPLE)
            return SUCCESS;
        if (
            sequence_heart != TYPE_CHAIN  // !!! temporary for //: -- review
            and Get_Flavor_Flag(SYMBOL, symbol, ILLEGAL_IN_ANY_SEQUENCE)
        ){
            goto bad_sequence_item;  //  [<| |>] => <|/|>  ; tag
        }
        if (sequence_heart == TYPE_PATH)
            return SUCCESS;
        if (Get_Flavor_Flag(SYMBOL, symbol, ILLEGAL_IN_TUPLE))
            goto bad_sequence_item;  // e.g. contains a slash
    }

    return SUCCESS;  // all other words should be okay

  bad_sequence_item:

    return Error_Bad_Sequence_Item_Raw(Datatype_From_Type(sequence_heart), e);
}


//=//// UNCOMPRESSED ARRAY SEQUENCE FORM //////////////////////////////////=//

#define Trap_Init_Any_Sequence_Listlike(out,heart,a) \
    Trap_Init_Any_Sequence_At_Listlike((out), (heart), (a), 0)


//=//// Leading-SPACE SEQUENCE OPTIMIZATION ///////////////////////////////=//
//
// Ren-C has no REFINEMENT! datatype, so `:foo` is a CHAIN!, which generalizes
// to where `:foo:bar` is a CHAIN! as well.
//
// In order to make this not cost more than a "REFINEMENT!" word type did in
// R3-Alpha, the underlying representation of `:foo` in the cell is the same
// as a WORD!.  This is true for `/foo` and `foo/` and `.foo` and `foo.` too.
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
    assert(flag == CELL_MASK_ERASED_0 or flag == CELL_FLAG_LEADING_SPACE);
    assert(Any_Sequence_Type(heart));

    Option(Error*) error = Trap_Check_Sequence_Element(
        heart,
        e,
        flag == CELL_MASK_ERASED_0  // 0 means no leading space, item is "head"
    );
    if (error)
        return error;

    if (Is_Word(e)) {  // see notes at top of file on `/a` cell optimization
        e->header.bits &= (~ CELL_FLAG_LEADING_SPACE);
        e->header.bits |= flag;
        KIND_BYTE(e) = heart;  // e.g. TYPE_WORD => TYPE_PATH
        return SUCCESS;
    }

    if (Any_List(e)) {  // try mirror optimization
        assert(Is_Group(e) or Is_Block(e));  // only valid kinds
        const Source* a = Cell_Array(e);
        Option(Heart) mirror = Mirror_Of(a);
        Option(Heart) h = Heart_Of(e);
        if (not mirror or ((unwrap mirror) == (unwrap h))) {
            MIRROR_BYTE(a) = unwrap Heart_Of(e);  // remember what kind it is
            KIND_BYTE(e) = heart;  // e.g. TYPE_BLOCK => TYPE_PATH
            e->header.bits |= flag;
            return SUCCESS;
        }
    }

    if (Is_Integer(e)) {
        if (heart == TYPE_TUPLE) {
            return Error_User(  // reserve notation for future use [1]
                "5. and .5 currently reserved, please use 5.0 and 0.5"
            );
        }
        // fallthrough (should be able to single cell optimize any INTEGER!)
    }

    Pairing* p = Alloc_Pairing(BASE_FLAG_MANAGED);
    if (flag == CELL_FLAG_LEADING_SPACE) {
        Init_Space(Pairing_First(p));
        Copy_Cell(Pairing_Second(p), e);
    }
    else {
        Copy_Cell(Pairing_First(p), e);
        Init_Space(Pairing_Second(p));
    }

    Reset_Cell_Header_Noquote(
        e,
        FLAG_HEART(heart)
            | (not CELL_FLAG_DONT_MARK_PAYLOAD_1)  // mark the pairing
            | CELL_FLAG_DONT_MARK_PAYLOAD_2  // payload second not used
    );
    Tweak_Cell_Binding(e, UNBOUND);  // "arraylike", needs binding
    CELL_SERIESLIKE_NODE(e) = p;
    Corrupt_Unused_Field(e->payload.split.two.corrupt);

    return SUCCESS;
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
// This will likely be easy to reuse in an RUNE!+CHAR! unification, so
// revisit this low-priority idea at that time.

INLINE Element* Init_Any_Sequence_Bytes(
    Init(Element) out,
    Heart heart,
    const Byte* data,
    Size size
){
    assert(Any_Sequence_Type(heart));
    Reset_Cell_Header_Noquote(
        out,
        FLAG_HEART(heart) | CELL_MASK_NO_MARKING
    );
    Tweak_Cell_Binding(out, UNBOUND);  // paths bindable, can't have garbage

    if (size > Size_Of(out->payload.at_least_8) - 1) {  // too big
        Source* a = Make_Source_Managed(size);
        for (; size > 0; --size, ++data)
            Init_Integer(Alloc_Tail_Array(a), *data);

        Init_Block(out, Freeze_Source_Shallow(a));  // !!! TBD: compact BLOB!
    }
    else {
        out->payload.at_least_8[IDX_SEQUENCE_USED] = size;
        Byte* dest = out->payload.at_least_8 + 1;
        for (; size > 0; --size, ++data, ++dest)
            *dest = *data;
    }

    return out;
}

#define Init_Tuple_Bytes(out,data,len) \
    Init_Any_Sequence_Bytes((out), TYPE_TUPLE, (data), (len));

INLINE Option(Element*) Try_Init_Any_Sequence_All_Integers(
    Init(Element) out,
    Heart heart,
    const Value* head,  // NOTE: Can't use PUSH() or evaluation
    REBLEN len
){
    assert(Any_Sequence_Type(heart));

    if (len > Size_Of(out->payload.at_least_8) - 1)
        return nullptr;  // no optimization yet if won't fit in payload bytes

    if (len < 2)
        return nullptr;

    Reset_Cell_Header_Noquote(
        out,
        FLAG_HEART(heart) | CELL_MASK_NO_MARKING
    );
    Tweak_Cell_Binding(out, UNBOUND);  // paths are bindable, can't be garbage

    out->payload.at_least_8[IDX_SEQUENCE_USED] = len;

    Byte* bp = out->payload.at_least_8 + 1;

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
    Init(Element) out,
    Heart heart,
    const Element* first,
    const Element* second
){
    assert(Any_Sequence_Type(heart));

    if (
        (Is_Quasar(first) and Is_Quasar(second))  // ~/~ is a WORD!
        or (Is_Space(first) and Is_Space(second))  // plain / is a WORD!
    ){
        if (heart == TYPE_PATH)
            Init_Word(out, CANON(SLASH_1));
        else if (heart == TYPE_CHAIN)
            Init_Word(out, CANON(COLON_1));
        else {
            assert(heart == TYPE_TUPLE);
            Init_Word(out, CANON(DOT_1));
        }
        if (Is_Quasar(first))
            Quasify_Isotopic_Fundamental(out);
        return SUCCESS;
    }

    if (Is_Space(first)) {  // try optimize e.g. `/a` or `.a` or `:a` etc.
        Copy_Cell(out, second);
        return Trap_Blank_Head_Or_Tail_Sequencify(
            out, heart, CELL_FLAG_LEADING_SPACE
        );
    }
    else if (Is_Space(second)) {
        Copy_Cell(out, first);
        return Trap_Blank_Head_Or_Tail_Sequencify(  // optimize `a/` or `a:`
            out, heart, CELL_MASK_ERASED_0
        );
    }

    if (Is_Integer(first) and Is_Integer(second)) {
        REBI64 i1 = VAL_INT64(first);
        REBI64 i2 = VAL_INT64(second);

        if (heart == TYPE_TUPLE) {  // conflates with decimal, e.g. 10.20
            REBI64 magnitude = 1;
            REBI64 r = i2;
            do {
                magnitude *= 10;
                r = r / 10;
            } while (r != 0);

            REBDEC d = cast(REBDEC, i1) + cast(REBDEC, i2) / magnitude;
            Init_Decimal(out, d);
            return SUCCESS;
        }

        if (heart == TYPE_CHAIN) {  // conflates with time, e.g. 10:20
            REBI64 nano = ((i1 * 60 * 60) + (i2 * 60)) * SEC_SEC;
            Init_Time_Nanoseconds(out, nano);
            return SUCCESS;
        }

        Byte buf[2];
        if (i1 >= 0 and i2 >= 0 and i1 <= 255 and i2 <= 255) {
            buf[0] = cast(Byte, i1);
            buf[1] = cast(Byte, i2);
            Init_Any_Sequence_Bytes(out, heart, buf, 2);
            return SUCCESS;
        }

        // fall through
    }

    Option(Error*) err1 = Trap_Check_Sequence_Element(heart, first, true);
    if (err1)
        return err1;

    Option(Error*) err2 = Trap_Check_Sequence_Element(heart, second, false);
    if (err2)
        return err2;

    Pairing* pairing = Alloc_Pairing(BASE_FLAG_MANAGED);
    Copy_Cell(Pairing_First(pairing), first);
    Copy_Cell(Pairing_Second(pairing), second);

    Reset_Cell_Header_Noquote(
        out,
        FLAG_HEART(heart)
            | (not CELL_FLAG_DONT_MARK_PAYLOAD_1)  // first is pairing
            | CELL_FLAG_DONT_MARK_PAYLOAD_2  // payload second not used
    );
    Tweak_Cell_Binding(out, UNBOUND);  // "arraylike", needs binding
    CELL_PAIRLIKE_PAIRING_NODE(out) = pairing;
    Corrupt_Unused_Field(out->payload.split.two.corrupt);

    return SUCCESS;
}


INLINE Option(Error*) Trap_Init_Any_Sequence_Pairlike(
    Init(Element) out,
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
        return Error_Conflated_Sequence_Raw(Datatype_Of(out), out);

    return SUCCESS;
}

INLINE Option(Error*) Trap_Pop_Sequence_Or_Conflation(
    Init(Element) out,
    Heart heart,
    StackIndex base
){
    if (TOP_INDEX - base < 2) {
        Drop_Data_Stack_To(base);
        return Error_Sequence_Too_Short_Raw();
    }

    if (TOP_INDEX - base == 2) {  // two-element path optimization
        Option(Error*) trap = Trap_Init_Any_Sequence_Or_Conflation_Pairlike(
            out,
            heart,
            TOP_ELEMENT - 1,
            TOP_ELEMENT
        );
        Drop_Data_Stack_To(base);
        return trap;
    }

    if (Try_Init_Any_Sequence_All_Integers(  // optimize e.g. 192.0.0.1
        out,
        heart,
        Data_Stack_At(Element, base) + 1,
        TOP_INDEX - base
    )){
        Drop_Data_Stack_To(base);  // optimization worked! drop stack...
        return SUCCESS;
    }

    assert(TOP_INDEX - base > 2);  // guaranteed from above
    Source* a = Pop_Managed_Source_From_Stack(base);
    Freeze_Source_Shallow(a);
    return Trap_Init_Any_Sequence_Listlike(out, heart, a);
}

INLINE Option(Error*) Trap_Pop_Sequence(
    Init(Element) out,
    Heart heart,
    StackIndex base
){
    Option(Error*) error = Trap_Pop_Sequence_Or_Conflation(out, heart, base);
    if (error)
        return error;

    if (not Any_Sequence(out))
        return Error_Conflated_Sequence_Raw(Datatype_Of(out), out);

    return SUCCESS;
}


// This is a general utility for turning stack values into something that is
// either pathlike or value like.  It is used in COMPOSE of paths, which
// allows things like:
//
//     >> compose $(void)/a
//     == a
//
//     >> compose $(space)/a
//     == /a
//
//     >> compose @ (void)/(void)/(void)
//     == ~null~  ; anti
//
// While you can't create a PATH! or TUPLE! out of just blanks, this function
// will decay two blanks in a path to the WORD! `/` and two blanks in a tuple
// to the WORD! '.' -- this could be extended to allow more blanks to get words
// like `///` if that were deemed interesting.
//
INLINE Option(Error*) Trap_Pop_Sequence_Or_Element_Or_Nulled(
    Init(Value) out,
    Heart sequence_heart,
    StackIndex base
){
    if (TOP_INDEX == base) {  // nothing to pop
        Init_Nulled(out);
        return SUCCESS;
    }

    if (TOP_INDEX - 1 == base) {  // only one item, use as-is if possible
        Move_Cell(out, TOP_ELEMENT);  // ensures element
        DROP();  // balances stack

        if (not Is_Space(out)) {  // allow _.(void) to be _ if COMPOSE'd
            Option(Error*) error = Trap_Check_Sequence_Element(
                sequence_heart,
                cast(Element*, out),
                false  // don't think of it as head, or do?
            );
            if (error)
                return error;
        }

        return SUCCESS;  // let the item just decay to itself as-is
    }

    return Trap_Pop_Sequence_Or_Conflation(out, sequence_heart, base);
}


// Note that paths can be initialized with an array, which they will then
// take as immutable...or you can create a `/foo`-style path in a more
// optimized fashion using Refinify()

INLINE Length Cell_Sequence_Len(const Cell* c) {
    assert(Any_Sequence_Type(Heart_Of(c)));

    if (not Sequence_Has_Pointer(c)) {  // compressed bytes
        assert(not Cell_Payload_2_Needs_Mark(c));
        return c->payload.at_least_8[IDX_SEQUENCE_USED];
    }

    const Base* payload1 = CELL_PAYLOAD_1(c);
    if (Is_Base_A_Cell(payload1))  // see if it's a pairing
        return 2;  // compressed 2-element sequence, sizeof(Stub)

    switch (Stub_Flavor(c_cast(Flex*, payload1))) {
      case FLAVOR_SYMBOL :  // compressed single WORD! sequence
        return 2;

      case FLAVOR_SOURCE : {  // uncompressed sequence
        const Source* a = c_cast(Source*, payload1);
        if (Mirror_Of(a))
            return 2;  // e.g. `(a):` stores TYPE_GROUP in the mirror byte
        assert(Array_Len(a) >= 2);
        assert(Is_Source_Frozen_Shallow(a));
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
//    are put into a sequence already have a Stub allocated, and that array
//    Stub has a place where size is written for non-array types when using
//    the small series optimization.
//
INLINE Element* Derelativize_Sequence_At(
    Sink(Element) out,
    const Element* sequence,
    Context* context,
    REBLEN n
){
    assert(out != sequence);
    assert(Any_Sequence_Type(Heart_Of(sequence)));  // !!! should not be cell

    if (not Sequence_Has_Pointer(sequence)) {  // compressed bytes
        assert(n < sequence->payload.at_least_8[IDX_SEQUENCE_USED]);
        return Init_Integer(out, sequence->payload.at_least_8[n + 1]);
    }

    const Base* payload1 = CELL_PAYLOAD_1(sequence);
    if (Is_Base_A_Cell(payload1)) {  // test if it's a pairing
        const Pairing* p = c_cast(Pairing*, payload1);  // compressed pair
        if (n == 0)
            return Derelativize(out, Pairing_First(p), context);
        assert(n == 1);
        return Derelativize(out, Pairing_Second(p), context);
    }

    switch (Stub_Flavor(u_c_cast(Flex*, payload1))) {
      case FLAVOR_SYMBOL : {  // compressed single WORD! sequence
        assert(n < 2);
        if (Get_Cell_Flag(sequence, LEADING_SPACE) ? n == 0 : n != 0)
            return Init_Space(out);

        Derelativize(out, sequence, context);  // [2]
        KIND_BYTE(out) = TYPE_WORD;
        LIFT_BYTE(out) = NOQUOTE_2;  // [3]
        return out; }

      case FLAVOR_SOURCE : {  // uncompressed sequence, or compressed "mirror"
        const Source* a = c_cast(Source*, CELL_SERIESLIKE_NODE(sequence));
        if (Mirror_Of(a)) {  // [4]
            assert(n < 2);
            if (Get_Cell_Flag(sequence, LEADING_SPACE) ? n == 0 : n != 0)
                return Init_Space(out);

            Derelativize(out, sequence, context);
            KIND_BYTE(out) = MIRROR_BYTE(a);
            LIFT_BYTE(out) = NOQUOTE_2;  // [3]
            return out;
        }
        assert(Array_Len(a) >= 2);
        assert(Is_Source_Frozen_Shallow(a));
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

INLINE Byte Cell_Sequence_Byte_At(const Element* sequence, REBLEN n) {
    DECLARE_ELEMENT (at);
    Copy_Sequence_At(at, sequence, n);
    if (not Is_Integer(at))
        panic ("Cell_Sequence_Byte_At() used on non-byte ANY-SEQUENCE?");
    return VAL_UINT8(at);  // !!! All callers of this routine need vetting
}

INLINE Context* Cell_Sequence_Binding(const Element* sequence) {
    assert(Any_Sequence_Type(Heart_Of(sequence)));

    // Getting the binding for any of the optimized types means getting
    // the binding for *that item in the sequence*; the sequence itself
    // does not provide a layer of communication connecting the interior
    // to a frame instance (because there is no actual layer).

    if (not Sequence_Has_Pointer(sequence))  // compressed bytes
        return SPECIFIED;

    const Base* payload1 = CELL_PAYLOAD_1(sequence);
    if (Is_Base_A_Cell(payload1))  // see if it's a pairing
        return Cell_Binding(sequence);  // compressed 2-element sequence

    switch (Stub_Flavor(c_cast(Flex*, payload1))) {
      case FLAVOR_SYMBOL:  // compressed single WORD! sequence
        return SPECIFIED;

      case FLAVOR_SOURCE: {  // uncompressed sequence
        const Source* a = Cell_Array(sequence);
        if (Mirror_Of(a))
            return SPECIFIED;
        return Cell_Binding(sequence); }

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
    assert(Heart_Of(tuple) == TYPE_TUPLE);
    if (not Try_Get_Sequence_Bytes(buf, tuple, buf_size))
        panic ("non-INTEGER! found used with Get_Tuple_Bytes()");
}

#define MAX_TUPLE \
    ((Size_Of(uint32_t) * 2))  // !!! No longer a "limit", review callsites



//=//// REFINEMENTS AND PREDICATES ////////////////////////////////////////=//


INLINE Element* Init_Set_Word(Init(Element) out, const Symbol* s) {
    Init_Word(out, s);
    KIND_BYTE(out) = TYPE_CHAIN;
    return out;
}

INLINE Element* Init_Get_Word(Init(Element) out, const Symbol* s) {
    Init_Word(out, s);
    KIND_BYTE(out) = TYPE_CHAIN;
    Set_Cell_Flag(out, LEADING_SPACE);
    return out;
}


INLINE Option(SingleHeart) Try_Get_Sequence_Singleheart(const Cell* c) {
    assert(Any_Sequence_Type(Heart_Of(c)));

    if (not Sequence_Has_Pointer(c))  // compressed bytes
        return NOT_SINGLEHEART_0;

    if (Is_Base_A_Cell(CELL_SERIESLIKE_NODE(c))) {
        const Pairing* p = cast(Pairing*, CELL_PAIRLIKE_PAIRING_NODE(c));

        if (Is_Space(Pairing_First(p)))
            return Leading_Space_And(Heart_Of_Builtin(Pairing_Second(p)));

        if (Is_Space(Pairing_Second(p)))
            return Trailing_Space_And(Heart_Of_Builtin(Pairing_First(p)));

        return NOT_SINGLEHEART_0;
    }

    const Flex* f = cast(Flex*, CELL_PAYLOAD_1(c));
    if (Stub_Flavor(f) == FLAVOR_SYMBOL) {
        if (c->header.bits & CELL_FLAG_LEADING_SPACE)
            return Leading_Space_And(TYPE_WORD);

        return Trailing_Space_And(TYPE_WORD);
    }

    Option(Heart) mirror = Mirror_Of(u_cast(const Source*, f));
    if (not mirror)  // s actually is sequence elements, not one element
        return NOT_SINGLEHEART_0;

    if (c->header.bits & CELL_FLAG_LEADING_SPACE)
        return Leading_Space_And(unwrap mirror);

    return Trailing_Space_And(unwrap mirror);
}


// GET-WORD! and SET-WORD!

INLINE bool Is_Get_Word_Cell(const Cell* c) {
    return (
        Heart_Of(c) == TYPE_CHAIN and
        LEADING_SPACE_AND(WORD) == Try_Get_Sequence_Singleheart(c)
    );
}

INLINE bool Is_Get_Word(const Value* v)
  { return LIFT_BYTE(v) == NOQUOTE_2 and Is_Get_Word_Cell(v); }

INLINE bool Is_Set_Word_Cell(const Cell* c) {
    return (
        Heart_Of(c) == TYPE_CHAIN and
        TRAILING_SPACE_AND(WORD) == Try_Get_Sequence_Singleheart(c)
    );
}

INLINE bool Is_Set_Word(const Value* v)
  { return LIFT_BYTE(v) == NOQUOTE_2 and Is_Set_Word_Cell(v); }


// The new /foo: assignment form ensures that the thing being assigned is
// an action.  Places that want to see this as a SET-WORD! (like WRAP) tend
// to be doing so because they want a symbol.  This is a combined interface
// that subsumes testing for both foo: and /foo: and gives the symbol.
//
INLINE Option(const Symbol*) Try_Get_Settable_Word_Symbol(
    Option(Sink(bool)) bound,
    const Element* e
){
    if (LIFT_BYTE(e) != NOQUOTE_2)
        return nullptr;
    if (Is_Set_Word_Cell(e)) {
        if (bound)
            *(unwrap bound) = IS_WORD_BOUND(e);
        return Cell_Word_Symbol(e);
    }
    if (Heart_Of(e) != TYPE_PATH)
        return nullptr;
    if (LEADING_SPACE_AND(CHAIN) != Try_Get_Sequence_Singleheart(e))
        return nullptr;  // e is not /?:?:? style path

    DECLARE_ELEMENT (temp);  // !!! should be able to optimize and not need this
    Derelativize_Sequence_At(temp, e, Cell_Sequence_Binding(e), 1);
    assert(Is_Chain(temp));

    if (TRAILING_SPACE_AND(WORD) != Try_Get_Sequence_Singleheart(temp))
        return nullptr;  // e is not /foo: style path

    if (bound)
        *(unwrap bound) = IS_WORD_BOUND(temp);
    return Cell_Word_Symbol(temp);
}


// GET-TUPLE! and SET-TUPLE!

INLINE bool Is_Get_Tuple_Cell(const Cell* c) {
    return (
        Heart_Of(c) == TYPE_CHAIN and
        LEADING_SPACE_AND(TUPLE) == Try_Get_Sequence_Singleheart(c)
    );
}

INLINE bool Is_Get_Tuple(const Value* v)
  { return LIFT_BYTE(v) == NOQUOTE_2 and Is_Get_Tuple_Cell(v); }

INLINE bool Is_Set_Tuple_Cell(const Cell* c) {
    return (
        Heart_Of(c) == TYPE_CHAIN and
        TRAILING_SPACE_AND(TUPLE) == Try_Get_Sequence_Singleheart(c)
    );
}

INLINE bool Is_Set_Tuple(const Value* v)
  { return LIFT_BYTE(v) == NOQUOTE_2 and Is_Set_Tuple_Cell(v); }


// GET-BLOCK! and SET-BLOCK!

INLINE bool Is_Get_Block_Cell(const Cell* c) {
    return (
        Heart_Of(c) == TYPE_CHAIN and
        LEADING_SPACE_AND(BLOCK) == Try_Get_Sequence_Singleheart(c)
    );
}

INLINE bool Is_Get_Block(const Value* v)
  { return LIFT_BYTE(v) == NOQUOTE_2 and Is_Get_Block_Cell(v); }

INLINE bool Is_Set_Block_Cell(const Cell* c) {
    return (
        Heart_Of(c) == TYPE_CHAIN and
        TRAILING_SPACE_AND(BLOCK) == Try_Get_Sequence_Singleheart(c)
    );
}

INLINE bool Is_Set_Block(const Value* v)
  { return LIFT_BYTE(v) == NOQUOTE_2 and Is_Set_Block_Cell(v); }


// GET-GROUP! and SET-GROUP!

INLINE bool Is_Get_Group_Cell(const Cell* c) {
    return (
        Heart_Of(c) == TYPE_CHAIN and
        LEADING_SPACE_AND(GROUP) == Try_Get_Sequence_Singleheart(c)
    );
}

INLINE bool Is_Get_Group(const Value* v)
  { return LIFT_BYTE(v) == NOQUOTE_2 and Is_Get_Group_Cell(v); }

INLINE bool Is_Set_Group_Cell(const Cell* c) {
    return (
        Heart_Of(c) == TYPE_CHAIN and
        TRAILING_SPACE_AND(GROUP) == Try_Get_Sequence_Singleheart(c)
    );
}

INLINE bool Is_Set_Group(const Value* v)
  { return LIFT_BYTE(v) == NOQUOTE_2 and Is_Set_Group_Cell(v); }


INLINE bool Any_Set_Value(const Value* v) {  // !!! optimize?
    Option(SingleHeart) single;
    return (
        LIFT_BYTE(v) == NOQUOTE_2
        and Heart_Of(v) == TYPE_CHAIN
        and (single = Try_Get_Sequence_Singleheart(v))
        and Singleheart_Has_Trailing_Space(unwrap single)
    );
}

INLINE bool Any_Get_Value(const Value* v) {  // !!! optimize?
    Option(SingleHeart) single;
    return (
        LIFT_BYTE(v) == NOQUOTE_2
        and Heart_Of(v) == TYPE_CHAIN
        and (single = Try_Get_Sequence_Singleheart(v))
        and Singleheart_Has_Leading_Space(unwrap single)
    );
}

INLINE Element* Refinify(Element* e) {
    Option(Error*) error = Trap_Blank_Head_Or_Tail_Sequencify(
        e, TYPE_CHAIN, CELL_FLAG_LEADING_SPACE
    );
    if (error)
        panic (unwrap error);
    return e;
}

#define Is_Refinement Is_Get_Word

INLINE const Symbol* Cell_Refinement_Symbol(const Cell* v) {
    assert(Is_Get_Word_Cell(v));
    return Cell_Word_Symbol(v);
}


// Degrade in place, simple singular chains like [a b]: -> [a b], or :a -> a
//
INLINE Element* Unchain(Element* out) {
    assert(Is_Chain(out));
    Option(Error*) error = Trap_Unsingleheart(out);
    assert(not error);
    UNUSED(error);
    return out;
}


// Degrade in-place, simple singular paths like [a b]/ -> [a b], or /a -> a
//
INLINE Element* Unpath(Element* out) {
    assert(Is_Path(out));
    Option(Error*) error = Trap_Unsingleheart(out);
    assert(not error);
    UNUSED(error);
    return out;
}

// Degrade in-place, simple singular tuples like [a b]. -> [a b], or .a -> a
//
INLINE Element* Untuple(Element* out) {
    assert(Is_Tuple(out));
    Option(Error*) error = Trap_Unsingleheart(out);
    assert(not error);
    UNUSED(error);
    return out;
}


INLINE Element* Blockify_Any_Sequence(Element* seq) {  // always works
    DECLARE_ELEMENT (temp);
    Option(Error*) e = Trap_Alias_Any_Sequence_As(temp, seq, TYPE_BLOCK);
    assert(not e);
    UNUSED(e);
    Copy_Cell(seq, temp);
    return seq;
}
