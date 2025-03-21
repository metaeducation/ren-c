//
//  File: %sys-isotope.h
//  Summary: "Isotope Coercion Routines"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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
// the logic for enforcing those rules centralized is important.  See the
// definition of ANTIFORM_0 for how we avoid other places in the code doing
// assignments to the quote byte without going through this function.
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

// 1. The convention here is that you have to pass an Atom in, because at
//    the end of the operation you'll have either a Value* or an Atom*.
//    If you were allowed to pass in an Element*, then you'd have an invalid
//    Element at the callsite when the operation completed.
//
INLINE Option(Error*) Trap_Coerce_To_Antiform(Need(Atom*) atom) {
    Element* elem = Known_Element(atom);  // guaranteed element on input (?)
    Heart heart = Heart_Of(atom);

    if (not Is_Stable_Antiform_Heart(heart))
        assert(not Is_Api_Value(elem));  // no unstable antiforms in API [1]

    if (not Any_Isotopic_Type(heart)) {
        QUOTE_BYTE(elem) = NOQUOTE_1;
        fail (Error_Non_Isotopic_Type_Raw(elem));
    }

    if (Is_Bindable_Heart(heart)) {  // strip off any binding [2]
        if (Any_Word_Type(heart)) {
            switch (Cell_Word_Id(elem)) {
              case SYM_NULL:
                assert(not Is_Api_Value(elem));  // API uses nullptr [3]
                break;

              case SYM_VOID:
              case SYM_OKAY:
              case SYM_NAN:
                break;

              default: {
                QUOTE_BYTE(elem) = NOQUOTE_1;
                return Error_Illegal_Keyword_Raw(elem);  // only a few ok [4]
              }
            }

            Unbind_Any_Word(elem);
        }
        else {
            assert(Any_List_Type(heart) or heart == TYPE_COMMA);
            Tweak_Cell_Binding(elem, UNBOUND);
        }
    }
    else if (heart == TYPE_FRAME) {
        if (Cell_Frame_Lens(elem))  // no lens on antiforms...show only inputs
            Tweak_Cell_Frame_Lens_Or_Label(elem, ANONYMOUS);
    }

    QUOTE_BYTE(atom) = ANTIFORM_0_COERCE_ONLY;  // nowhere else should assign!
    return nullptr;  // no error
}

// 1. There's an exception in the case of KEYWORD! which is the antiform of
//    WORD!.  Only a limited set of them are allowed to exist.  But all
//    words are allowed to be quasiforms.
//
INLINE Option(Error*) Trap_Coerce_To_Quasiform(Need(Element*) v) {
    Heart heart = Heart_Of(v);

    if (not Any_Isotopic_Type(heart)) {  // Note: all words have quasiforms [1]
        QUOTE_BYTE(v) = NOQUOTE_1;
        return Error_Non_Isotopic_Type_Raw(v);
    }

    QUOTE_BYTE(v) = QUASIFORM_2_COERCE_ONLY;  // few places should assign
    return nullptr;
}


// When you're sure that the value isn't going to be consumed by a multireturn
// then use this to get the first value unmeta'd
//
// 1. We fall through in case result is pack or raised (should this iterate?)
//
// 2. If the first element in a pack is a pack, we could decay that.  Maybe
//    we should, but my general feeling is that you should probably have to
//    unpack more granularly, e.g. ([[a b] c]: packed-pack), and it would just
//    create confusion to decay things automatically.  Experience may dictate
//    that decaying automatically here is better, wait and see.
//
// 3. If there's a raised error in the non-primary slot of a pack, we don't
//    want to just silently discard it.  The best way to make sure no packs
//    are hiding errors is to recursively decay.
//
INLINE Value* Decay_If_Unstable(Need(Atom*) v) {
    if (Not_Antiform(v))
        return u_cast(Value*, u_cast(Atom*, v));

    if (Is_Lazy(v)) {  // should this iterate?
        if (not Pushed_Decaying_Level(v, v, LEVEL_MASK_NONE))
            return u_cast(Value*, u_cast(Atom*, v));  // cheap reification
        if (Trampoline_With_Top_As_Root_Throws())
            fail (Error_No_Catch_For_Throw(TOP_LEVEL));
        Drop_Level(TOP_LEVEL);
        assert(not Is_Lazy(v));
    }

    if (Is_Pack(v)) {  // iterate until result is not multi-return [1]
        const Element* pack_meta_tail;
        const Element* pack_meta_at = Cell_List_At(&pack_meta_tail, v);
        if (pack_meta_at == pack_meta_tail)
            fail (Error_No_Value_Raw());  // treat as void?
        Derelativize(v, pack_meta_at, Cell_List_Binding(v));
        Meta_Unquotify_Undecayed(v);
        if (Is_Pack(v) or Is_Lazy(v))
            fail (Error_Bad_Antiform(v));  // need more granular unpacking [2]
        if (Is_Raised(v))
            fail (Cell_Error(v));
        assert(Not_Antiform(v) or Is_Antiform_Stable(v));

        while (++pack_meta_at != pack_meta_tail) {
            if (not Is_Quasiform(pack_meta_at))
                continue;
            if (Is_Stable_Antiform_Heart(Heart_Of(pack_meta_at)))
                continue;
            DECLARE_ATOM (temp);
            Copy_Cell(temp, pack_meta_at);
            Decay_If_Unstable(temp);  // don't drop raised errors on floor [3]
        }

        return u_cast(Value*, u_cast(Atom*, v));
    }

    if (Is_Barrier(v))
        fail (Error_No_Value_Raw());  // distinct error from nihil?

    if (Is_Raised(v))
        fail (Cell_Error(v));

    return u_cast(Value*, u_cast(Atom*, v));
}


// Packs with unstable isotopes in their first cell are not able to be decayed.
// Type checking has to be aware of this, and know that such packs shouldn't
// raise errors.
//
INLINE bool Is_Pack_Undecayable(Atom* pack)
{
    assert(Is_Pack(pack));
    if (Is_Nihil(pack))
        return true;
    const Element* at = Cell_List_At(nullptr, pack);
    if (Is_Meta_Of_Raised(at))
        return true;
    if (Is_Meta_Of_Pack(at))
        return true;
    if (Is_Meta_Of_Lazy(at))
        return true;
    return false;
}
