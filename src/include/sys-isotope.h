//
//  file: %sys-isotope.h
//  summary: "Isotope Coercion Routines"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2024 Ren-C Open Source Contributors
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
// There are several rules that have to be followed with antiforms.  Having
// the logic for enforcing those rules centralized is important.
//
// 1. Unstable antiforms are not legal in API handles.  They are analogous
//    to variables, and if you need to deal in the currency of unstable
//    antiforms--and ^META conventions aren't enough--see rebDelegate() and
//    rebContinuation() for how to work around it.
//
// 2. Antiforms are not allowed to carry bindings.  Though something like a
//    slice or a pack can have bindings on the elements, the container itself
//    is not allowed to be bound.
//
//    (If antiforms did have meaningful bindings, that would imply binding
//    functions would need to accept them as parameters.  That would lead to a
//    mess--trying to handle unstable pack antiforms via meta-parameters.)
//
// 3. The API uses nullptr as currency with C or other languages to represent
//    the nulled state.  This allows it to be "falsey" in those languages, as
//    well as to not require cleanup by releasing a handle for it.  So you
//    should never be initializing an API value with the cell pattern used
//    internally to represent ~null~ antiforms.  Centrally enforcing that
//    here helps avoiding needing to check it everywhere else.
//
// 4. While all WORD!s are allowed to have quasiforms, only special ones are
//    allowed to be antiform "keywords".  Other words are reserved for future
//    usage, though dialects can use quasi words however they want.
//
// 1. The convention here is that you have to pass an Value in, because at
//    the end of the operation you'll have either a Stable* or an Value*.
//    If you were allowed to pass in an Element*, then you'd have an invalid
//    Element at the callsite when the operation completed.
//
INLINE Result(Value*) Coerce_To_Antiform(Need(Value*) atom) {
    Element* elem = Known_Element(atom);  // guaranteed element on input (?)

    if (Underlying_Sigil_Of(elem))
        return fail (Error_User("Cells with sigils cannot become antiforms"));

    Option(Heart) heart = Heart_Of(atom);

    if (not Is_Stable_Antiform_Kind_Byte(u_cast(KindByte, heart)))
        assert(not Is_Api_Value(elem));  // no unstable antiforms in API [1]

    if (not Any_Isotopic_Type(heart)) {
        LIFT_BYTE(elem) = NOQUOTE_2;
        return fail (Error_Non_Isotopic_Type_Raw(elem));
    }

    if (Is_Bindable_Heart(heart)) {  // strip off any binding [2]
        if (heart == TYPE_WORD) {
            elem->header.bits &= ~(
                CELL_FLAG_TYPE_SPECIFIC_A | CELL_FLAG_TYPE_SPECIFIC_B
            );

            switch (opt Word_Id(elem)) {
              case SYM_NULL:
                assert(not Is_Api_Value(elem));  // API uses nullptr [3]
                Set_Cell_Flag(elem, KEYWORD_IS_NULL);
                break;

              case SYM_OKAY:
              case SYM_NAN:
                break;

              default: {
                LIFT_BYTE(elem) = NOQUOTE_2;
                return fail (Error_Illegal_Keyword_Raw(elem));  // limited [4]
              }
            }

            Unbind_Any_Word(elem);
        }
        else if (heart == TYPE_FENCE) {  // canonize datatypes
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
            // !!! don't mess with flags (e.g. SLOT_WEIRD_MARKED_DUAL)
            atom->payload = Stub_Cell(unwrap patch)->payload;
            atom->extra = Stub_Cell(unwrap patch)->extra;
        }
        else {
            assert(Any_List_Type(heart) or heart == TYPE_COMMA);
            Tweak_Cell_Binding(elem, UNBOUND);
        }
    }
    else if (heart == TYPE_FRAME) {
        if (Frame_Lens(elem))  // no lens on antiforms...show only inputs
            Tweak_Frame_Lens_Or_Label(elem, ANONYMOUS);
    }

    LIFT_BYTE_RAW(atom) = ANTIFORM_1;  // few places should use LIFT_BYTE_RAW!
    return atom;
}

// 1. There's an exception in the case of KEYWORD! which is the antiform of
//    WORD!.  Only a limited set of them are allowed to exist.  But all
//    words are allowed to be quasiforms.
//
INLINE Result(Element*) Coerce_To_Quasiform(Need(Element*) v) {
    Option(Heart) heart = Heart_Of(v);

    if (not Any_Isotopic_Type(heart)) {  // Note: all words have quasiforms [1]
        LIFT_BYTE(v) = NOQUOTE_2;
        return fail (Error_Non_Isotopic_Type_Raw(v));
    }

    LIFT_BYTE_RAW(v) = QUASIFORM_3;  // few places should use LIFT_BYTE_RAW!
    return u_cast(Element*, v);
}


//=//// ELIDING AND DECAYING UNSTABLE ANTIFORMS ///////////////////////////=//
//
// Decay is the process of producing a stable value from an unstable one.  It
// is not legal to decay an unstable antiform into another unstable antiform,
// and it's too risky to let ERROR!s inside PACK!s be silently discarded...
// so they have to be elevated to panics.
//
// "Elision" is more permissive than decay, because you're not actually trying
// to extract a value if the antiform is a PACK! or GHOST! (or a PACK! with
// a PACK! in the first slot, which must be unpacked vs. auto-decaying).  So
// you only need to be concerned about sweeping any ERROR!s under the rug.
//
// The concern about searching for embedded ERROR!s is shared between the
// decay and elide routines, so they are implemented using a common function.
//

#define Decay_If_Unstable(v) \
    Decay_Or_Elide_Core((v), true)

#define Elide_Unless_Error_Including_In_Packs(v) \
    Decay_Or_Elide_Core((v), false)

INLINE Result(Stable*) Decay_Or_Elide_Core(
    Need(Value*) v,
    bool want_value  // ELIDE is more permissive, doesn't want the value
){
    if (Not_Antiform(v))
        goto finished;

    if (not Is_Pack(v)) {
        if (want_value and Is_Ghost(v))
            return fail ("Cannot decay GHOST! to a value");

        if (Is_Error(v))
            return fail (Cell_Error(v));

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
    const Element* first = List_At(&tail, v);

    if (want_value and first == tail)
        return fail ("Empty PACK! cannot decay to single value");

    for (const Element* at = first; at != tail; ++at) {
        if (not Any_Lifted(at))
            return fail ("Non-lifted element in PACK!");

        if (Is_Lifted_Error(at))
            return fail (Cell_Error(at));

        if (not Is_Lifted_Pack(at))
            continue;

        Copy_Cell(v, at);
        assume (
            Unliftify_Undecayed(v)
        );
        trap (  /// elide recursively to look for hidden ERROR! [1]
            Elide_Unless_Error_Including_In_Packs(v)
        );
    }

    if (want_value) {
        if (Is_Lifted_Pack(first))  // don't decay first slot [2]
            return fail ("PACK! cannot decay PACK! in first slot");

        if (Is_Lifted_Ghost(first))
            return fail ("PACK! cannot decay GHOST! in first slot");

        assert(not Is_Lifted_Error(first));  // we ruled these out already

        Copy_Cell(v, first);  // Note: no antiform binding (PACK!)
        assume (  // Any_Lifted() already checked for all pack items
            Unliftify_Undecayed(v)
        );
    }

    goto finished;

} finished: { ////////////////////////////////////////////////////////////////

  #if NEEDFUL_DOES_CORRUPTIONS
    if (not want_value)
       Corrupt_Cell_If_Needful(v);
  #endif

    return u_cast(Stable*, v);
}}

INLINE Result(Stable*) Unliftify_Decayed(Stable* v) {
    trap (
      Value *atom = Unliftify_Undecayed(cast(Value*, v))
    );
    return Decay_If_Unstable(atom);
}
