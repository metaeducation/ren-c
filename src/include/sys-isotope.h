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

INLINE Cell* Coerce_To_Antiform(Cell* c) {
    Heart heart = Cell_Heart(c);

    if (not Is_Stable_Antiform_Heart(heart))
        assert(not Is_Api_Value(c));  // no unstable antiforms in API [1]

    if (not Any_Isotopic_Kind(heart)) {
        QUOTE_BYTE(c) = NOQUOTE_1;
        fail (Error_Non_Isotopic_Type_Raw(c));
    }

    if (Is_Bindable_Heart(heart)) {  // strip off any binding [2]
        if (Any_Word_Kind(heart)) {
            switch (Cell_Word_Id(c)) {
              case SYM_NULL:
                assert(not Is_Api_Value(c));  // API uses nullptr [3]
                break;

              case SYM_VOID:
              case SYM_OKAY:
              case SYM_END:
              case SYM_NAN:
                break;

              default:
                QUOTE_BYTE(c) = NOQUOTE_1;
                fail (Error_Illegal_Keyword_Raw(c));  // only a few legal [4]
            }

            Unbind_Any_Word(c);
        }
        else {
            assert(Any_List_Kind(heart));
            BINDING(c) = UNBOUND;
        }
    }

    QUOTE_BYTE(c) = ANTIFORM_0_COERCE_ONLY;  // nowhere else should assign!
    return c;
}

INLINE Value* Coerce_To_Stable_Antiform(Value* v) {
    assert(Is_Stable_Antiform_Heart(Cell_Heart(v)));
    return cast(Value*, Coerce_To_Antiform(v));
}

INLINE Atom* Coerce_To_Unstable_Antiform(Atom* a) {
    assert(not Is_Stable_Antiform_Heart(Cell_Heart(a)));
    return cast(Atom*, Coerce_To_Antiform(a));
}

INLINE Element* Coerce_To_Quasiform(Value* v) {
    Heart heart = Cell_Heart(v);

    if (not Any_Isotopic_Kind(heart)) {
        QUOTE_BYTE(v) = NOQUOTE_1;
        fail (Error_Non_Isotopic_Type_Raw(v));
    }

    QUOTE_BYTE(v) = QUASIFORM_2_COERCE_ONLY;  // few places should assign
    return cast(Element*, v);
}

INLINE Option(Context*) Trap_Coerce_To_Quasiform(Value* v) {
    Heart heart = Cell_Heart(v);

    if (not Any_Isotopic_Kind(heart)) {
        QUOTE_BYTE(v) = NOQUOTE_1;
        return Error_Non_Isotopic_Type_Raw(v);
    }

    QUOTE_BYTE(v) = QUASIFORM_2_COERCE_ONLY;  // few places should assign
    return nullptr;
}
