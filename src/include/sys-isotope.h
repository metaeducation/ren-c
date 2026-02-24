//
//  file: %sys-isotope.h
//  summary: "Isotope Coercion Routines"
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


#undef Any_Isotopic  // use Any_Isotopic_Type(Heart_Of(v))


// There are several rules that have to be followed with antiforms.  Having
// the logic for enforcing those rules centralized is important.
//
// 1. The convention here is that you have to pass an Value in, because at
//    the end of the operation you'll have either a Stable* or an Value*.
//    If you were allowed to pass in an Element*, then you'd have an invalid
//    Element at the callsite when the operation completed.
//
//    Note: When building with ExactWrapper, Exact(T) isn't free... costs code
//    in dereferencing.  So it's useful to extract it into an Element* first.
//
// 2. The API uses nullptr as currency with C or other languages to represent
//    the nulled state.  This allows it to be "falsey" in those languages, as
//    well as to not require cleanup by releasing a handle for it.  So you
//    should never be initializing an API value with the cell pattern used
//    internally to represent ~null~ antiforms.
//
//    To avoid risks of this being something that *could* happen, we disallow
//    coercion of API values to antiforms--as a general rule.
//
INLINE Result(Value*) Coerce_To_Antiform(Exact(Value*) v){  // [1]
    Element* elem = As_Element(v);  // efficient unwrapped extraction [1]

    assert(not Is_Api_Value(elem));  // API uses nullptr, not nulled cells [2]

  ensure_elem_is_quasiform_with_no_sigil: {

  // Quasiforms are allowed to have Sigils, e.g. ~@~ is legal.  But for the
  // moment, there are no antiforms defined for Sigilized types.  This isn't
  // a technical limitation, it's just that the set of antiforms is limited
  // on purpose--to reserve the meanings for future use.

    if (
        (elem->header.bits & (FLAG_TYPE_BYTE(255) | CELL_MASK_SIGIL))
            != FLAG_TYPE(TYPE_QUASIFORM)
    ){
        if (Type_Of(elem) != TYPE_QUASIFORM)
            return fail (
                Error_User("Can only coerce quasiforms to antiforms")
            );
        return fail (Error_User("Cells with sigils cannot become antiforms"));
    }

} coerce_to_antiform: {

  // 1. Antiforms can't be bound.  Though SPLICE! or PACK! can have bindings
  //    on the *elements*, the containing list is not allowed to be bound.
  //
  //    (If antiforms did have meaningful bindings, that would imply binding
  //    functions would need to accept them as parameters.  That leads to a
  //    mess--trying to handle unstable pack antiforms via meta-parameters.)
  //
  // 2. All WORD!s are allowed to have quasiforms, but only NULL and OKAY are
  //    allowed to be antiforms.  Reserving others for future use had only
  //    nebulous benefit and created uncertainty, while having just those two
  //    states provides a solid LOGIC! type (vs. type constraint).

    Option(Heart) heart = Heart_Of(elem);

    switch (opt heart) {
      case HEART_FRAME_SIGNIFYING_ACTION: {
        if (Frame_Lens(elem))
            Tweak_Frame_Lens_Or_Label(elem, ANONYMOUS);  // show only inputs
        Force_Phase_Final(Frame_Phase(elem));
        TYPE_BYTE(v) = TYPE_ACTION;
        break; }

      case HEART_BLOCK_SIGNIFYING_SPLICE: {
        Tweak_Cell_Binding(elem, UNBOUND);  // [1]
        TYPE_BYTE(v) = TYPE_SPLICE;
        break; }

      case HEART_GROUP_SIGNIFYING_PACK: {
        Tweak_Cell_Binding(elem, UNBOUND);  // [1]
        TYPE_BYTE(v) = TYPE_PACK;
        break; }

      case HEART_FENCE_SIGNIFYING_DATATYPE: {
        Option(Patch*) patch;
        if (
            Series_Len_At(elem) != 1
            or not Is_Word(List_Item_At(elem))
            or not (patch = Sea_Patch(
                g_datatypes_context,
                Word_Symbol(List_Item_At(elem)),
                true
            ))
        ){
            return fail (elem);
        }
        v->payload = Stub_Cell(unwrap patch)->payload;
        v->extra = Stub_Cell(unwrap patch)->extra;
        TYPE_BYTE(v) = TYPE_DATATYPE;
        break; }

      case HEART_WORD_SIGNIFYING_LOGIC: {
        Unbind_Any_Word(elem);  // [1]
        switch (opt Word_Id(elem)) {
          case SYM_OKAY:
            TYPE_BYTE(v) = TYPE_LOGIC_OKAY;
            break;

          case SYM_NULL:
            TYPE_BYTE(v) = TYPE_LOGIC_NULL;
            break;

          default:
            return fail (Error_Illegal_Anti_Word_Raw(elem));  // limited [2]
        }
        break; }

      case HEART_TAG_SIGNIFYING_TRASH:
        Freeze_Flex(Cell_Strand(v));  // !!! intern if WORD-like! ?
        TYPE_BYTE(v) = TYPE_TRASH;
        break;

      case HEART_BLANK_SIGNIFYING_VOID:
        TYPE_BYTE(v) = TYPE_VOID;
        break;

      case HEART_ERROR_SIGNIFYING_FAILURE:
        TYPE_BYTE(v) = TYPE_FAILURE;
        break;

      default:
        return fail (Error_Non_Isotopic_Type_Raw(elem));
    }

    return v;
}}


// 1. There's an exception in the case of KEYWORD! which is the antiform of
//    WORD!.  Only a limited set of them are allowed to exist.  But all
//    words are allowed to be quasiforms.
//
INLINE Result(Element*) Coerce_To_Quasiform(Element* v) {
    Option(Heart) heart = Heart_Of(v);

    if (not Any_Isotopic_Type(heart)) {  // Note: all words have quasiforms [1]
        Clear_Cell_Quotes_And_Quasi(v);
        return fail (Error_Non_Isotopic_Type_Raw(v));
    }

    TYPE_BYTE_RAW(v) = QUASIFORM_64;  // few places should use TYPE_BYTE_RAW!
    return v;
}


//=//// ELIDING AND DECAYING UNSTABLE ANTIFORMS ///////////////////////////=//
//
// Decay is the process of producing a stable value from an unstable one.  It
// is not legal to decay an unstable antiform into another unstable antiform,
// and it's too risky to let FAILURE!s inside PACK!s be silently discarded...
// so they have to be elevated to panics.
//
// "Elision" is more permissive than decay, because you're not actually trying
// to extract a value if the antiform is a PACK! or VOID! (or a PACK! with
// a PACK! in the first slot, which must be unpacked vs. auto-decaying).  So
// you only need to be concerned about sweeping any FAILURE!s under the rug.
//
// The concern about searching for embedded FAILURE!s is shared between the
// decay and elide routines, so they are implemented using a common function.
//
// 1. We don't want to cast away the error state, but we don't want to give
//    back a Stable* value either if we didn't decay.  Casting to Result(None)
//    can be made to work in C++ but it can't work in C because that would
//    be casting a pointer to an enum.  Use inline function, it's easier.
//
// 2. If you want a value, then in the general case of getters and setters
//    that involves running code, which can't be done from an intrinsic.
//    This would foil things like ELIDE... except they just say they don't
//    want a value.  This raises some questions about undecayables that
//    need greater study.

INLINE Result(Stable*) Decay_Or_Elide_Core(Value* v, bool want_value);

#define Decay_If_Unstable(v) \
    Decay_Or_Elide_Core(Possibly_Unstable(v), true)

INLINE Result(None) Ensure_No_Failures_Including_In_Packs_Core(
    const Value* v  // this as a macro is a nightmare, use a function [1]
){
    trap (
      Decay_Or_Elide_Core(m_cast(Value*, v), false)
    );
    return none;
}

#define Ensure_No_Failures_Including_In_Packs(v) \
    Ensure_No_Failures_Including_In_Packs_Core(Possibly_Unstable(v))

INLINE Result(Stable*) Decay_Or_Elide_Core(
    Value* v,
    bool want_value  // ELIDE is more permissive, doesn't want the value
){
    if (want_value)  // eval required in general case (getters, alias) [2]
        assert(Not_Level_Flag(TOP_LEVEL, DISPATCHING_INTRINSIC));

    if (Is_Cell_Stable(v))
        goto finished;

    if (not Is_Pack(v)) {
        if (Is_Failure(v))
            return fail (Cell_Error(v));

        if (not want_value)
            goto finished_no_value;

        if (Is_Action(v)) {
            Deactivate_Action(v);
            goto finished;
        }

        if (Is_Void(v))
            return fail ("Cannot decay VOID! to a stable value");

        if (Is_Trash(v))
            return fail ("Cannot decay TRASH! to a stable value");

        goto finished;
    }

  handle_pack: {  // iterate until result is not a pack

  // 1. If there's an antiform error in the non-primary slot of a pack, we
  //    don't want to just silently discard it.  The best way to make sure no
  //    packs are hiding errors is to recursively decay.
  //
  // 2. If the first element in a pack is a pack, we could decay that.  Maybe
  //    we should, but my general feeling is that you should probably have to
  //    unpack more granularly, e.g. ([[a b] c]: packed-pack), and it just
  //    creates confusion to decay things automatically.  Experience may
  //    dictate that decaying automatically here is better, wait and see.

    const Element* tail;
    const Dual* first = u_cast(Dual*, List_At(&tail, v));

    if (want_value and first == tail)
        return fail ("Empty PACK! cannot decay to single value");

    for (const Dual* at = first; at != tail; ++at) {
        if (not Any_Lifted(at)) {
            if (Is_Dual_Alias(at))  // !!! new concept
                continue;  // !!! try this for alias first, others later...

            return fail ("Non-lifted element in PACK!");
        }

        if (Is_Lifted_Failure(at))
            return fail (Cell_Error(at));

        if (not Is_Lifted_Pack(at))
            continue;

        Copy_Cell(v, at);
        assume (
            Unlift_Cell_No_Decay(v)
        );
        Decay_Or_Elide_Core(v, false) except (Error* e) {  // [1]
            return fail (e);
        }
    }

    if (not want_value)
        goto finished_no_value;

    if (Is_Lifted_Unstable_Antiform(first)) {  // don't decay first slot [2]
        if (Is_Lifted_Pack(first))
            return fail ("PACK! cannot decay PACK! in first slot");

        if (Is_Lifted_Void(first))
            return fail ("PACK! cannot decay VOID! in first slot");

        if (Is_Lifted_Trash(first))
            return fail ("PACK! cannot decay TRASH! in first slot");

        assert(Is_Lifted_Action(first));
        return fail ("PACK! cannot decay ACTION! in first slot");
    }

    assert(not Is_Lifted_Failure(first));  // we ruled these out already

    if (Is_Dual_Alias(first)) {
        trap (
          Get_Word_Or_Tuple(v, first)
        );
        require (
          Decay_If_Unstable(v)
        );
    }
    else {
        Copy_Cell(v, first);  // Note: no antiform binding (PACK!)
        assume (  // Any_Lifted() already checked for all pack items
            Unlift_Cell_No_Decay(v)
        );
    }

    goto finished;

} finished: { ////////////////////////////////////////////////////////////////

    return As_Stable(v);

} finished_no_value: { ///////////////////////////////////////////////////////

    return nullptr;
}}

INLINE Result(Stable*) Unliftify_Decayed(Stable* v) {
    trap (
      Value *unlifted = Unlift_Cell_No_Decay(cast(Value*, v))
    );
    return Decay_If_Unstable(unlifted);
}
