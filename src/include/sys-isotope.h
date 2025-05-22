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
    Option(Heart) heart = Heart_Of(atom);

    if (not Is_Stable_Antiform_Heart(heart))
        assert(not Is_Api_Value(elem));  // no unstable antiforms in API [1]

    if (not Any_Isotopic_Type(heart)) {
        QUOTE_BYTE(elem) = NOQUOTE_1;
        return Error_Non_Isotopic_Type_Raw(elem);
    }

    if (Sigil_Of(elem))
        return Error_User("Cells with sigils cannot become antiforms");

    if (Is_Bindable_Heart(heart)) {  // strip off any binding [2]
        if (heart == TYPE_WORD) {
            switch (Cell_Word_Id(elem)) {
              case SYM_NULL:
                assert(not Is_Api_Value(elem));  // API uses nullptr [3]
                break;

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
        else if (heart == TYPE_FENCE) {  // canonize datatypes
            Option(Patch*) patch;
            if (
                Cell_Series_Len_At(elem) != 1
                or not Is_Word(Cell_List_Item_At(elem))
                or not (patch = Sea_Patch(
                    g_datatypes_context,
                    Cell_Word_Symbol(Cell_List_Item_At(elem)),
                    true
                ))
            ){
                return Error_Bad_Value(elem);
            }
            Copy_Cell(atom, cast(Atom*, Stub_Cell(unwrap patch)));
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
    return SUCCESS;
}

// 1. There's an exception in the case of KEYWORD! which is the antiform of
//    WORD!.  Only a limited set of them are allowed to exist.  But all
//    words are allowed to be quasiforms.
//
INLINE Option(Error*) Trap_Coerce_To_Quasiform(Need(Element*) v) {
    Option(Heart) heart = Heart_Of(v);

    if (not Any_Isotopic_Type(heart)) {  // Note: all words have quasiforms [1]
        QUOTE_BYTE(v) = NOQUOTE_1;
        return Error_Non_Isotopic_Type_Raw(v);
    }

    QUOTE_BYTE(v) = QUASIFORM_2_COERCE_ONLY;  // few places should assign
    return SUCCESS;
}


// Some packs (e.g. those with lifted unstbale antiforms in them) can't be
// decayed automatically.  They must be explicitly unpacked.
//
// Type checking has to be aware of this, and know that such packs shouldn't
// return errors.
//
// 1. It is very atypical to allow unstable antiforms in a pack.  But if you
//    do, then they could be masking an arbitrary amount of things like
//    errors (e.g. errors in a PACK!)  The limited use cases for unstable
//    antiforms in packs must unpack them, and not just allow them to drop
//    into non-existence just because they weren't requested in an unpack.
//
// 2. An antiform block that contains non-lifted Elements *could* have those
//    Elements convey a "dual representation".  e.g. a FRAME! could be
//    interpreted as "be the accessor function for what you assign to".
//    That's a novel concept, but better to use SET:DUAL and GET:DUAL and
//    avoid the overhead of a PACK! to weirdly encode the idea.
//
INLINE bool Is_Pack_Undecayable(Atom* pack)
{
    assert(Is_Pack(pack));

    const Element* tail;
    const Element* at = Cell_List_At(&tail, pack);

    if (at == tail)  // Is_Void() empty pack... not decayable
        return true;

    for (; at != tail; ++at) {  // all pack elements get checked [1]
        if (QUOTE_BYTE(at) >= ONEQUOTE_NONQUASI_3)
            continue;  // most common case, lifted normal Elements

        if (QUOTE_BYTE(at) == QUASIFORM_2) {
            if (Is_Stable_Antiform_Heart(Heart_Of(at)))
                continue;  // lifted stable antiform, decayable

            return true;  // lifted unstable antiform... not decayable
        }

        assert(QUOTE_BYTE(at) == NOQUOTE_1);
        return true;  // today we consider this corrupt [2]
    }

    return false;
}


// When you're sure that the value isn't going to be consumed by a multireturn
// then use this to get the first value unlift'd
//
// 1. We fall through in case result is pack or error (should this iterate?)
//
// 2. If the first element in a pack is a pack, we could decay that.  Maybe
//    we should, but my general feeling is that you should probably have to
//    unpack more granularly, e.g. ([[a b] c]: packed-pack), and it would just
//    create confusion to decay things automatically.  Experience may dictate
//    that decaying automatically here is better, wait and see.
//
// 3. If there's an antiform error in the non-primary slot of a pack, we don't
//    want to just silently discard it.  The best way to make sure no packs
//    are hiding errors is to recursively decay.
//
INLINE Value* Decay_If_Unstable(Need(Atom*) v) {
    if (Not_Antiform(v))
        return u_cast(Value*, u_cast(Atom*, v));

    if (Is_Pack(v)) {  // iterate until result is not multi-return [1]
        if (Is_Pack_Undecayable(v))
            panic ("Undecayable pack in Decay_If_Unstable()");

        const Element* pack_at = Cell_List_At(nullptr, v);
        Sink(Element) sink = v;
        Copy_Cell(sink, pack_at);  // Note: no antiform binding (PACK!)
        Unliftify_Undecayed(v);
        return u_cast(Value*, u_cast(Atom*, v));
    }

    if (Is_Ghost(v))
        panic (Error_No_Value_Raw());  // distinct error from void?

    if (Is_Error(v))
        panic (Cell_Error(v));

    return u_cast(Value*, u_cast(Atom*, v));
}
