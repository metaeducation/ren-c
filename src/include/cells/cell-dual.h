//
//  file: %cell-dual.h
//  summary: "Definitions for Special Cell Dual States"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2025 Ren-C Open Source Contributors
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
// Bedrock states are states that live "underneath" Value*.
//
// They can only be stored in Slot* Cells (if they can be stored at all),
// and so cannot be held in Value*.
//
// If you exchange values by a lifted convention, then any NOQUOTE_2 states
// that are not QUOTED! or QUASIFORM! can be used to represent these bedrock
// states.  We refer to this multiplexing as a "Dual".
//


#define Is_Bedrock(cell) \
    (LIFT_BYTE(Possibly_Bedrock(cell)) == BEDROCK_255)

#define Is_Dualized_Bedrock(dual) \
    (LIFT_BYTE(Known_Dual(dual)) == NOQUOTE_3)


//=//// UNDECAYED ~(...)~ BEDROCK PACK!s //////////////////////////////////-//
//
// Another way you can represent bedrock states is a their dual states in
// an antiform PACK!.  This is because dual states are never quoted or quasi,
// and ordinary Value* in a PACK! are always LIFT-ed to either quoted or
// quasi states.
//
// This PACK! form is known as "undecayed" bedrock...because it reverses a
// trick used in the decay process that turns unlifted antiforms inside PACK!
// into bedrock on assignment:
//
//     >> x: 1020
//
//     >> y: alias $x
//     == \~(^x)~\  ; antiform (pack!) "alias" <-- undecayed bedrock
//
//     >> y: 304
//
//     >> x
//     == 304
//
// This may seem kind of dicey compared to being more controlled, and making
// you go through a variable, e.g. (alias $y $x).  But when building higher
// level functions that want to work with bedrock, it's convenient:
//
//    static: lambda [
//        []: [bedrock?]
//        @init [block! fence!]
//    ][
//        if init <> '[static-storage] {  ; first run if not equal
//            static-storage: eval init   ; v-- mutate to [static-storage]
//            insert clear mutable init $static-storage
//        }
//        alias init.1
//    ]
//
// If you couldn't make aliasing a "return value", then static would have to
// take the thing to assign as the left hand side.  Less fun, more awkward!
//

INLINE const Dual* Opt_Extract_Dual_If_Undecayed_Bedrock(const Value* v) {
    if (not Is_Pack(v))
        return nullptr;

    const Element* tail;
    const Dual* item = u_cast(Dual*, List_At(&tail, v));
    if (item == tail or item + 1 != tail)
        return nullptr;

    if (LIFT_BYTE(item) != NOQUOTE_3)
        return nullptr;

    return item;
}

#define Is_Undecayed_Bedrock(v) \
    u_cast(bool, Opt_Extract_Dual_If_Undecayed_Bedrock(v))


//=//// DRAIN BEDROCK: SPACE //////////////////////////////////////////////-//
//
// This is what slots are set to when you do things like:
//
//    for-each _ [1 2 3] [...]
//
// It makes some amount of sense that the dual would be a SPACE rune.
//
//    >> drain
//    == \~(_)~\  ; antiform (pack!) "drain"
//
// Note: The name "blackhole" was originally used for this concept, but the
// need for bedrock PARAMETER! to be a "hole" made that term confusing.
//

INLINE bool Is_Bedrock_Dual_A_Drain(const Dual* dual) {
    assert(Is_Dualized_Bedrock(dual));
    return Is_Space(dual);  // maybe no faster than Is_Dual_Drain()?
}

#define Is_Drain_Core(cell, lift_byte) \
    Is_Cell_Space_With_Lift_Sigil((cell), (lift_byte), SIGIL_0)

#define Is_Cell_A_Bedrock_Drain(cell) \
    Is_Drain_Core(Possibly_Bedrock(cell), BEDROCK_255)

#define Is_Dual_Drain(dual) \
    Is_Drain_Core(Known_Dual(dual), NOQUOTE_3)

INLINE Slot* Init_Bedrock_Drain(Init(Slot) out) {
    Init_Space(out);
    LIFT_BYTE(out) = BEDROCK_255;
    return out;
}

INLINE bool Is_Undecayed_Drain(const Value* v) {  // ~(_)~ PACK!
    const Dual* dual = Opt_Extract_Dual_If_Undecayed_Bedrock(v);
    return dual and Is_Dual_Drain(dual);
}


//=//// "HOT-POTATO" : WORD! ///////////////////////////////////////-//
//
// WORD! duals are specifically prohibited from being stored in variables
// -or- decaying, leading them to be a lightweight way of making something
// that is "FAILURE!-like" which can only be taken as a ^META form.
//
// 1. VETO hot potatoes signal a desire to cancel the operation that requested
//    the evaluation.  Unlike VOID which opts out of slots but keeps running,
//    many operations that observe a VETO will return NULL:
//
//        >> reduce ["a" ^void "b"]
//        == ["a" "b"]
//
//        >> reduce ["a" veto "b"]
//        == \~null~\  ; antiform
//
//    In PARSE, a GROUP! that evaluates to VETO doesn't cancel the parse,
//    but rather just fails that specific GROUP!'s combinator, rolling over to
//    the next alternate:
//
//        >> parse [a b] ['a (if 1 < 2 [veto]) 'b | (print "alt!") 'a 'b]
//        alt!
//        == 'b
//
// 2. DONE hot potatoes report that an enumeration is exhausted and has no
//    further items to give back.  They're used by YIELD or functions that
//    want to act as generators for looping constructs like FOR-EACH or MAP:
//
//        count: 0
//        make-one-thru-five: func [
//            return: [done? integer!]
//        ][
//            if count = 5 [return done]
//            return count: count + 1
//        ]
//
//        >> map 'i make-one-thru-five/ [i * 10]
//        == [10 20 30 40 50]
//
// 3. RETRY is used by loops for AGAIN, to do the loop body without doing any
//    incrementation of the loop variables.
//

INLINE bool Is_Hot_Potato_With_Id_Core(
    Value* v,
    Option(SymId) id,
    LiftByte lift_byte
){
    if (not Cell_Has_Lift_Sigil_Heart(v, lift_byte, SIGIL_0, TYPE_GROUP))
        return false;

    const Element* tail;
    const Element* item = List_At(&tail, v);
    if (item == tail or item + 1 != tail)
        return false;
    if (not id)
        return Is_Word(item);
    return Is_Word_With_Id(item, unwrap id);
}

#define Is_Hot_Potato_With_Id(v, id) \
    Is_Hot_Potato_With_Id_Core( \
        known(Value*, (v)), (id), UNSTABLE_ANTIFORM_1)

#define Is_Lifted_Hot_Potato_With_Id(v, id) \
    Is_Hot_Potato_With_Id_Core( \
        known(Value*, (v)), (id), QUASIFORM_4)

#define Is_Hot_Potato(v) \
    Is_Hot_Potato_With_Id((v), none)

#define Is_Lifted_Hot_Potato(v) \
    Is_Lifted_Hot_Potato_With_Id((v), none)


#define Is_Cell_A_Veto_Hot_Potato(v) \
    Is_Hot_Potato_With_Id((v), SYM_VETO)  // [1]

#define Is_Cell_A_Done_Hot_Potato(v) \
    Is_Hot_Potato_With_Id((v), SYM_DONE)  // [2]

#define Is_Cell_A_Retry_Hot_Potato(v) \
    Is_Hot_Potato_With_Id((v), SYM_RETRY)  // [3]


//=//// HOLE BEDROCK: PARAMETER! //////////////////////////////////////////-//
//
// A hole is a PARAMETER! slot that has not been specialized yet.  So long
// as specialization has not occurred, the parameter specification is there
// including the types that are legal.
//
// Assignmnents to a HOLE should probably typecheck; but traditionally this
// typechecking is forgotten after the assignment.
//
// To things like DEFAULT, HOLE looks like the variable is not set.
//

INLINE bool Is_Bedrock_Dual_A_Hole(const Dual* dual) {
    assert(Is_Dualized_Bedrock(dual));
    return (
        KIND_BYTE(dual) == Kind_From_Sigil_And_Heart(SIGIL_0, TYPE_PARAMETER)
    );
}

#define Is_Hole_Core(cell,lift_byte) \
    Cell_Has_Lift_Sigil_Heart((cell), (lift_byte), SIGIL_0, TYPE_PARAMETER)

#define Is_Cell_A_Bedrock_Hole(cell) \
    Is_Hole_Core(Possibly_Bedrock(cell), BEDROCK_255)

#define Is_Dual_Hole(dual) \
    Is_Hole_Core(Known_Dual(dual), NOQUOTE_3)

INLINE bool Is_Undecayed_Hole(const Value* v) {  // ~(parameter!)~ PACK!
    const Dual* dual = Opt_Extract_Dual_If_Undecayed_Bedrock(v);
    return dual and Is_Dual_Hole(dual);
}


//=//// ALIAS BEDROCK: META-WORD!, META-TUPLE! ////////////////////////////-//
//
// An alias lets one variable act as another.
//
//    >> x: 10
//
//    >> y: alias $x
//    == \~(^x)~\  ; antiform (pack!) "alias"
//
//    >> y: 20
//
//    >> x
//    == 20
//
// It is chosen as a ^META signal because the least amount of mutation is
// needed to make it something compatible with a SET and GET operation that
// can set to anything (the decision to decay or not is done before the
// alias is written or read from).

INLINE bool Is_Bedrock_Dual_An_Alias(const Dual* dual) {
    assert(Is_Dualized_Bedrock(dual));
    return (
        KIND_BYTE(dual) == Kind_From_Sigil_And_Heart(SIGIL_META, TYPE_WORD)
        or KIND_BYTE(dual) == Kind_From_Sigil_And_Heart(SIGIL_META, TYPE_TUPLE)
    );
}

INLINE bool Is_Alias_Core(const Cell* cell, LiftByte lift_byte) {
    return Cell_Has_Lift_Sigil_Heart(
        cell, lift_byte, SIGIL_META, TYPE_WORD
    ) or Cell_Has_Lift_Sigil_Heart(
        cell, lift_byte, SIGIL_META, TYPE_TUPLE
    );
}

#define Is_Cell_A_Bedrock_Alias(cell) \
    Is_Alias_Core(Possibly_Bedrock(cell), BEDROCK_255)

#define Is_Dual_Alias(dual) \
    Is_Alias_Core(Known_Dual(dual), NOQUOTE_3)

INLINE bool Is_Undecayed_Alias(const Value* v) {  // ~(^meta)~ PACK!
    const Dual* dual = Opt_Extract_Dual_If_Undecayed_Bedrock(v);
    return dual and Is_Dual_Alias(dual);
}


//=//// ACCESSOR BEDROCK: FRAME! //////////////////////////////////////////-//
//
// An accessor function can serve as a GETTER or a SETTER for processing
// assignmetns via GET and SET (or GET-WORD/SET-WORD).
//

INLINE bool Is_Bedrock_Dual_An_Accessor(const Dual* dual) {
    assert(Is_Dualized_Bedrock(dual));
    return KIND_BYTE(dual) == Kind_From_Sigil_And_Heart(SIGIL_0, TYPE_FRAME);
}

#define Is_Accessor_Core(cell,lift_byte) \
    Cell_Has_Lift_Sigil_Heart((cell), (lift_byte), SIGIL_0, TYPE_FRAME)

#define Is_Cell_A_Bedrock_Accessor(cell) \
    Is_Accessor_Core(Possibly_Bedrock(cell), BEDROCK_255)

#define Is_Dual_Accessor(dual) \
    Is_Accessor_Core(Known_Dual(dual), NOQUOTE_3)

#define Is_Dual_Word_Named_Signal(dual)  Is_Word(Known_Dual(dual))


//=//// DUAL PICKING //////////////////////////////////////////////////////=//
//
// PICK is built on top of TWEAK, which uses the "dual protocol" to return
// the picked result.  This means that if it returns a lifted value (such as
// a quoted or quasiform) the value existed in the original structure as the
// unlifted representation of that lifted form.
//
// 1. The data structure implementing the TWEAK hook may be able to store null
//    values literally (e.g. a BLOCK! can't, but an OBJECT! can).  If it can
//    store nulls, then returning a lifted null in the dual protocol means
//    "the value was present, and it was null".  But returning a non-lifted
//    null means "the value was not present" (e.g. asking for a field in an
//    object that doesn't exist).
//
//    A hook should only return NULL_OUT_SLOT_UNAVAILABLE if the picker made
//    sense, but the slot just isn't there.  So if you pass an OBJECT! as the
//    picker for a BLOCK!, that should be an error condition vs. NULL.
//
// 2. If a data structure is storing bedrock slots, such as an ALIAS or a
//    GETTER, then picking it will return the dual of that state for the core
//    machinery to handle (as opposed to making arbitrary containers worry
//    about how to dispatch those states).
//

#define LIFT_OUT_FOR_DUAL_PICK \
    x_cast(Bounce, Lift_Cell(OUT))

#define LIFT_NULL_OUT_FOR_DUAL_PICK \
    x_cast(Bounce, Lift_Cell(Init_Null(OUT)))  // lifted null: present + null

#define NULL_OUT_SLOT_UNAVAILABLE  NULL_OUT  // non-lifted null: not present [1]

#define Init_Null_Signifying_Slot_Unavailable(cell)  Init_Null(cell)
#define Is_Null_Signifying_Slot_Unavailable(cell)  Is_Null(cell)

#define Init_Null_Signifying_Tweak_Is_Pick(dual)  Init_Null(dual)
#define Is_Null_Signifying_Tweak_Is_Pick(dual)  Is_Null(dual)

#define OUT_UNLIFTED_DUAL_INDIRECT_PICK \
    (assert(not Any_Lifted(OUT)), x_cast(Bounce, OUT))  // e.g. ALIAS, GETTER


//=//// DUAL POKING ///////////////////////////////////////////////////////=//
//
// POKE is also built on top of TWEAK, and uses the dual protocol to receive
// the value to be poked into the data structure... as well as using dual
// states to signal conditions of the return.  A key to the design is that
// if a tweak has to manipulate parts of a Cell that may be stored in a
// container, it has to return the bits that will be written back.
//
// So for instance: if you have a TIME! value you want to poke into a DATE!,
// the time bits live in the same Cell as the date.  So once the pattern is
// updated, persisting this in a container (such as an OBJECT! field which
// held the original date) requires telling the poking machinery what bits
// to write back.
//

#define LIFT_OUT_FOR_DUAL_WRITEBACK \
    x_cast(Bounce, Lift_Cell(OUT))  // commentary

#define Init_Okay_Signifying_No_Writeback(dual) \
    Init_Okay(dual)

#define OKAY_OUT_NO_WRITEBACK \
    x_cast(Bounce, Init_Okay_Signifying_No_Writeback(OUT))

#define Is_Okay_Signifying_No_Writeback(cell)  Is_Okay(cell)

#define OUT_UNLIFTED_DUAL_INDIRECT_POKE \
    (assert(not Any_Lifted(OUT)), x_cast(Bounce, OUT))  // e.g. ALIAS, SETTER


// !!! Places that use this should probably be running through the central
// SET/TWEAK code.  Also, it's not clear if this should just be Is_Void().
//
INLINE bool Is_Non_Meta_Assignable_Unstable_Antiform(const Value* v)
  { return Is_Void(v) or Is_Trash(v) or Is_Action(v); }

INLINE bool Is_Lifted_Non_Meta_Assignable_Unstable_Antiform(const Stable* v)
 { return Is_Lifted_Void(v) or Is_Lifted_Trash(v) or Is_Lifted_Action(v); }
