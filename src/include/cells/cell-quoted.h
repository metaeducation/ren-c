//
//  file: %cell-quoted.h
//  summary: "Definitions for QUOTED! Cells"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2018-2025 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Cells reserve a byte in their header called the TYPE_BYTE().  The most
// basic usage is that any value can be "quote" escaped.  The depth is the
// number of apostrophes, e.g. ''''X is a depth of 4.  The operator QUOTE can
// be used to add a quoting level to a value, UNQUOTE to remove one, and
// NOQUOTE to remove all quotes.
//
//     >> quote [a]
//     == '[a]
//
//     >> noquote first ['''''a]
//     == a
//
// But the TYPE_BYTE() is used to encode other states as well: some datatypes
// (besides QUOTED! itself) have an "antiform" form as well as a "quasi" form.
// The quasi form will evaluate to the antiform, and the antiform is expressly
// prohibited from being put in lists.
//
//     >> quasiform: first [~null~]
//     == ~null~
//
//     >> quasiform
//     == ~null~
//
//     >> append [a b c] quasiform
//     == [a b c ~null~]
//
//     >> antiform: ~null~  ; quasiforms evaluates to their antiforms
//     == \~null~\  ; antiform (logic!)
//
//     >> append [a b c] antiform
//     ** PANIC: ...
//
// Antiforms are new in Ren-C, and central to how the design solves historical
// problems in Rebol languages.
//

INLINE Count Quotes_Of(const Element* v) {
    assert(Type_Of_Raw(v) < MIN_TYPE_ANTIFORM);
    if (Type_Of_Raw(v) <= MAX_TYPE_NOQUOTE_QUASI_OK)
        return 0;
    return (TYPE_BYTE(v) - NOQUOTE_63) >> 1;
}

INLINE Count Quotes_From_Lift_Byte(TypeEnum lift) {
    assert(lift < MIN_TYPE_ANTIFORM);
    if (lift <= MAX_TYPE_NOQUOTE_QUASI_OK)
        return 0;
    return (i_cast(TypeByte, lift) - NOQUOTE_63) >> 1;
}

// Fast test for quotedness when you know the value can't be an antiform (e.g.
// it's an Element).
//
#define Is_Quoted(v) \
    (Type_Of_Raw(Readable_Cell(Known_Element(v))) > MAX_TYPE_NOQUOTE_QUASI_OK)

// Slower test for quotedness when you can't rule out antiforms (you have to
// check a second range of the TYPE_BYTE).
//
INLINE bool Is_Cell_Quoted_Core(const Cell* cell) {
    assert(Type_Of_Raw(cell) != BEDROCK_255);
    if (Type_Of_Raw(cell) <= MAX_TYPE_NOQUOTE_QUASI_OK)
        return false;
    if (Type_Of_Raw(cell) >= MIN_TYPE_ANTIFORM)
        return false;
    return true;  // all "middle" values are quoted
}

#define Is_Possibly_Unstable_Value_Quoted(v) \
    (Is_Cell_Quoted_Core(Readable_Cell(v)))

#define Is_Cell_Quoted(v) \
    Is_Cell_Quoted_Core(Readable_Cell(Known_Stable(v)))

#define Any_Fundamental(v) \
    (Type_Of_Raw(Readable_Cell(Known_Stable(v))) <= MAX_TYPE_NOQUOTE_NOQUASI)

#define Is_Quoted_Form_Of(heartname, v) \
    Cell_Has_Lift_Sigil_Heart(Known_Stable(v), \
        ONEQUOTE_NONQUASI_65, SIGIL_0, TYPE_##heartname)


// Turns X into 'X, or '''[1 + 2] into '''''(1 + 2), etc.
//
INLINE Element* Quotify_Depth(Element* v, Count depth) {
    assert(Type_Of_Raw(v) < MIN_TYPE_ANTIFORM);

    if (depth == 0)
        return v;

    if (Quotes_Of(v) + depth >  MAX_QUOTE_DEPTH_64)
        panic ("Quoting Depth of 64 Exceeded");

    if (Type_Of_Raw(v) <= MAX_TYPE_NOQUOTE_NOQUASI)
        TYPE_BYTE_RAW(v) = NOQUOTE_63;
    else
        possibly(TYPE_BYTE(v) == QUASIFORM_64);  // want to keep quasi bit

    TYPE_BYTE_RAW(v) += Quote_Shift(depth);

    return v;
}

#define Quote_Cell(v)  Quotify_Depth((v), 1)

#define Lift_From_Sigil(sigil) \
    i_cast(TypeEnum, (sigil))

INLINE Element* Unquote_Quoted_Cell(Element* v) {
    assert(Type_Of_Raw(v) > MAX_TYPE_NOQUOTE_QUASI_OK);

    TYPE_BYTE_RAW(v) -= Quote_Shift(1);
    possibly(TYPE_BYTE_RAW(v) == QUASIFORM_64);
    if (TYPE_BYTE_RAW(v) == NOQUOTE_63) {
        Sigil sigil = i_cast(Sigil, HEARTSIGIL_BYTE_RAW(v) >> BYTE_SIGIL_SHIFT);
        if (sigil)
            TYPE_BYTE(v) = Lift_From_Sigil(sigil);
        else
            TYPE_BYTE(v) = i_cast(TypeEnum, HEARTSIGIL_BYTE(v));
    }
    return v;
}

INLINE void Normalize_Cell(Cell* cell) {  // drop quoted/quasi/anti, keep sigil
    possibly(Type_Of_Raw(cell) == BEDROCK_255);  // normalizes bedrock too
    if (Type_Of_Raw(cell) < MIN_TYPE_HEART) {
        possibly(Type_Of_Raw(cell) == TYPE_0_constexpr);  // extended, no Sigil
        assert(
            (HEARTSIGIL_BYTE_RAW(cell) >> BYTE_SIGIL_SHIFT) == TYPE_BYTE_RAW(cell)
        );
    }
    else if (Type_Of_Raw(cell) <= MAX_TYPE_HEART) {
        assert(TYPE_BYTE_RAW(cell) == HEARTSIGIL_BYTE_RAW(cell));  // no Sigil, equal
    }
    else {
        possibly(TYPE_BYTE_RAW(cell) == QUASIFORM_64);
        Sigil sigil = i_cast(Sigil, HEARTSIGIL_BYTE_RAW(cell) >> BYTE_SIGIL_SHIFT);
        if (sigil)
            TYPE_BYTE(cell) = Lift_From_Sigil(sigil);
        else
            TYPE_BYTE(cell) = i_cast(TypeEnum, HEARTSIGIL_BYTE(cell));
    }
}

#define Clear_Cell_Quotes_And_Quasi(v) \
    Normalize_Cell(known(Element*, v))

INLINE Count Noquotify_Cell(Element* elem) {
    Count quotes = Quotes_Of(elem);
    if (quotes == 0)
        return 0;
    if (TYPE_BYTE_RAW(elem) & NONQUASI_BIT)
        Clear_Cell_Quotes_And_Quasi(elem);  // no quasi to remove, so it's ok
    else
        TYPE_BYTE_RAW(elem) = QUASIFORM_64;  // already quasi
    return quotes;
}

INLINE bool Have_Matching_Lift_Levels(const Stable* a, const Stable* b) {
    if (Type_Of_Raw(a) <= MAX_TYPE_NOQUOTE_NOQUASI)
        return Type_Of_Raw(b) <= MAX_TYPE_NOQUOTE_NOQUASI;
    if (Type_Of_Raw(b) <= MAX_TYPE_NOQUOTE_NOQUASI)
        return false;
    return Type_Of_Raw(a) == Type_Of_Raw(b);
}


//=//// ANTIFORMS /////////////////////////////////////////////////////////=//
//
// Antiforms are foundational in covering edge cases in representation which
// plague Rebol2 and Red.  They enable shifting into a "non-literal" domain,
// where whatever "weird" condition the antiform was attempting to capture can
// be handled without worrying about conflating with more literal usages.
//
// A good example is addressing the splicing intent for blocks:
//
//     >> append [a b c] [d e]
//     == [a b c [d e]]
//
//     >> ~[d e]~
//     == \~[d e]~\  ; antiform (splice!)
//
//     >> append [a b c] ~[d e]~
//     == [a b c d e]
//
//     >> append [a b c] '~[d e]~
//     == [a b c ~[d e]~]
//
// As demonstrated, the reified QUASIFORM! and the non-reified ANTIFORM! work
// in concert to solve the problem.
//
// 1. Each antiform gets a synthetic TYPE_XXX enum value, and these states are
//    all numerically greater than the TYPE_XXX for non-antiforms.
//

#if CHECK_CELL_SUBCLASSES
    INLINE bool Is_Antiform(const Value* v) {
        assert(Type_Of_Raw(v) != BEDROCK_255);
        return Type_Of_Raw(Readable_Cell(v)) >= MIN_TYPE_ANTIFORM;
    }

    INLINE bool Is_Antiform(const Element* v) = delete;
#else
    #define Is_Antiform(v) \
        (Type_Of_Raw(Readable_Cell(v)) >= MIN_TYPE_ANTIFORM)
#endif

#define Not_Antiform(v) (not Is_Antiform(v))

#undef Any_Antiform  // Is_Antiform() faster than auto-generated macro [1]

#define Is_Lifted_Antiform(v) \
    (TYPE_BYTE(Readable_Cell(v)) == QUASIFORM_64)


//=//// UNSTABLE ANTIFORMS ////////////////////////////////////////////////=//
//
// Unstable antiforms like PACK!, FAILURE!, and VOID! antiforms aren't just
// not allowed in blocks, they can't be in stored in "normal" variables
// (only ^META variables can hold them).  They will either decay to stable
// forms or cause errors in decay.
//
// The ^META parameter convention must be used to get unstable antiforms.
// Code that isn't expecting such strange circumstances can panic if they
// happen, while more sensitive code can be adapted to cleanly handle the
// intents that they care about.
//
// 1. When antiforms are created, they use two distinct lift states: one for
//    stable and another for unstable.  This makes testing for antiforms
//    "slower" (have to test >= MIN_TYPE_ANTIFORM v. simple equality), but
//    it makes testing for unstable antiforms very fast (which is nice, since
//    that has to be done very often).
//
// 2. With the optimization in [1], there's not a real benefit to having a
//    narrower check for instability for cells you know are antiforms.  But
//    before that optimization it did help...and it doesn't hurt for code to
//    be more clear about what it knows, so the separate entry point is kept.
//
// 3. We don't sacrifice a Cell flag for "this would be unstable if unlifted"
//    but only have a fast test using a dedicated unlifted state.  So a
//    quasiform must be tested for the exact heart masks.
//

#if NO_RUNTIME_CHECKS
    #define Is_Cell_Stable(v) \
        (TYPE_BYTE(v) <= MAX_TYPE_STABLE)  // it's really fast! [1]

    #define Is_Antiform_Stable  Is_Cell_Stable  // equivalent [2]
#else
    INLINE bool Is_Cell_Stable_Core(const Value* v) {  // careful checks...
        possibly(not Is_Antiform(v));  // general check for any Value

        Assert_Cell_Readable(v);
        assert(Type_Of_Raw(v) != BEDROCK_255);

        bool stable = (Type_Of_Raw(v) <= MAX_TYPE_STABLE);

      #if RUNTIME_CHECKS
        if (stable)
            assert(
                Not_Antiform(v)
                or Is_Stable_Antiform_Heart(Heart_Of_Unsigiled_Isotopic(v))
            );
        else
            assert(
                Is_Antiform(v)
                and Not_Stable_Antiform_Heart(Heart_Of_Unsigiled_Isotopic(v))
            );
      #endif

        return stable;
    }

    #define Is_Cell_Stable(v) \
        Is_Cell_Stable_Core(Possibly_Unstable(v))

    INLINE bool Is_Antiform_Stable_Core(const Value* a) {  // narrow check [2]
        unnecessary(Readable_Cell(a));  // Is_Antiform() checked readable
        assert(Type_Of_Raw(a) >= MIN_TYPE_ANTIFORM);
        impossible(0 != (a->header.bits & CELL_MASK_SIGIL));
        return Is_Cell_Stable(a);
    }

    #define Is_Antiform_Stable(v) \
        Is_Antiform_Stable_Core(Possibly_Unstable(v))
#endif

#define Not_Cell_Stable(v)  (not Is_Cell_Stable(v))
#define Not_Antiform_Stable(v)  (not Is_Antiform_Stable(v))

INLINE bool Is_Lifted_Unstable_Antiform(const Value* v) {  // costs more [3]
    unnecessary(Readable_Cell(v));  // assume Is_Antiform() checked readable

    if (TYPE_BYTE(v) != QUASIFORM_64)
        return false;  // not quasiform so not lifted unstable

    possibly(0 != (v->header.bits & CELL_MASK_SIGIL));  // quasi sigils ok

    Heart heart = unwrap Heart_Of(v);  // no quasiform extended types
    return Not_Stable_Antiform_Heart(heart);
}

#if NO_RUNTIME_CHECKS
    #define Assert_Cell_Stable(v)  NOOP
#else
    #define Assert_Cell_Stable(v) \
        assert(Is_Cell_Stable(cast(Value*, (v))))
#endif

MUTABLE_IF_C(Result(Stable*), INLINE) Ensure_Stable(CONST_IF_C(Value*) v_) {
    CONSTABLE(Value*) v = m_cast(Value*, v_);
    if (Not_Cell_Stable(v))
        return fail ("Value is an unstable antiform");
    return u_cast(Stable*, v);
}


//=//// ENSURE THINGS ARE ELEMENTS ////////////////////////////////////////=//
//
// An array element can't be an antiform.  Use As_Element() when you are
// sure you have an element and only want it checked in the debug build, and
// Ensure_Element() when you are not sure and want to panic if not.
//

MUTABLE_IF_C(Option(Element*), INLINE) Try_As_Element(CONST_IF_C(Stable*) v_) {
    CONSTABLE(Stable*) v = m_cast(Stable*, Readable_Cell(v_));
    if (Is_Antiform(v))
        return nullptr;
    return As_Element(v);
}

MUTABLE_IF_C(Element*, INLINE) Ensure_Element(CONST_IF_C(Value*) cell) {
    CONSTABLE(Value*) v = m_cast(Value*, cell);
    if (Type_Of_Raw(v) >= MIN_TYPE_ANTIFORM)
        panic (Error_Bad_Antiform(v));
    return As_Element(v);
}

#if CHECK_CELL_SUBCLASSES
    void Ensure_Element(const Element*) = delete;
#endif


//=//// QUASIFORM! ////////////////////////////////////////////////////////=//

// * Quasiforms are truthy.  There's a reason for this, because it allows
//   operations in the ^META domain to easily use functions like ALL and ANY
//   on the lifted values.  (See the FOR-BOTH example.)

INLINE Result(Value*) Coerce_To_Antiform(Exact(Value*) atom);
INLINE Result(Element*) Coerce_To_Quasiform(Exact(Element*) v);

#define Is_Quasiform(v) \
    (TYPE_BYTE(Readable_Cell(Known_Stable(v))) == QUASIFORM_64)

INLINE Element* Unquasify(Element* elem) {
    assert(TYPE_BYTE(elem) == QUASIFORM_64);
    Clear_Cell_Quotes_And_Quasi(elem);
    return elem;
}

INLINE Element* Quasify_Isotopic_Fundamental(Element* elem) {
    assert(Any_Isotopic_Type(Heart_Of(elem)));
    assert(Type_Of_Raw(elem) <= MAX_TYPE_NOQUOTE_NOQUASI);
    TYPE_BYTE_RAW(elem) = QUASIFORM_64;
    return elem;
}

INLINE Element* Quasify_Antiform(Exact(Stable*) v) {
    assert(Is_Antiform(v));
    TYPE_BYTE_RAW(v) = QUASIFORM_64;  // all antiforms can be quasi
    return u_cast(Element*, v);
}

INLINE Element* Reify_If_Antiform(Value* v) {
    if (Type_Of_Raw(v) < MIN_TYPE_ANTIFORM)
        return As_Element(v);
    assert(Type_Of_Raw(v) != BEDROCK_255);
    TYPE_BYTE_RAW(v) = QUASIFORM_64;  // all antiforms can become quasi
    return As_Element(v);
}


//=//// ANTIFORMIZE FUNDAMENTALS /////////////////////////////////////////=//
//
// !!! Warning: Internal use only, this bypasses typical checks: go through
// the Coerce_To_Antiform() function instead unless you know what you
// are doing.
//

INLINE void Antiformize_Unbound_Fundamental(Value* v, Type lift) {
    assert(Heart_Of(v) != HEART_WORD_SIGNIFYING_LOGIC);  // no LOGIC_IS_OKAY
    assert(Type_Of_Raw(v) <= MAX_TYPE_NOQUOTE_NOQUASI);
    possibly(Not_Stable_Antiform_Heart(Heart_Of_Unsigiled_Isotopic(v)));
    if (Is_Bindable_Heart(Unchecked_Heart_Of(v)))
        assert(not Cell_Binding(v));
    TYPE_BYTE_RAW(v) = i_cast(TypeByte, lift);
}


//=//// LIFTING ///////////////////////////////////////////////////////////=//

// Lifting is a superset of plain quoting.  It has the twist that it can
// "quote antiforms" to produce quasiforms.  This is done by LIFT, but also on
// assignment by metaforms (^foo: ...) and metaforms UNLIFT when fetching.
//
// It's hard to summarize in one place all the various applications of this
// feature!  But it's critical to accomplishing composability by which a
// usermode function can accomplish what the system is able to do internally
// with C.  See FOR-BOTH for at least one good example.
//
//  https://forum.rebol.info/t/1833
//

INLINE bool Not_Lifted(const Value* v) {
    Assert_Cell_Readable(v);
    if (TYPE_BYTE_RAW(v) < QUASIFORM_64)
        return true;  // fundamental
    if (Type_Of_Raw(v) >= MIN_TYPE_ANTIFORM)
        return true;  // antiform
    return false;  // quoted or quasi
}

#define Any_Lifted(v) \
    (not Not_Lifted(v))  // anti or fundamental

INLINE Dual* Lift_Cell(Value* v) {
    if (Type_Of_Raw(v) < MIN_TYPE_ANTIFORM)
        return As_Dual(Quote_Cell(As_Element(v)));  // non-antiform -> quoted

    assert(Type_Of_Raw(v) != BEDROCK_255);
    TYPE_BYTE_RAW(v) = QUASIFORM_64;  // both unstable and stable become quasi
    return As_Dual(v);
}

INLINE Result(Value*) Unlift_Cell_No_Decay_Core(Value* v) {
    if (TYPE_BYTE_RAW(v) == QUASIFORM_64) {
        trap (
          Coerce_To_Antiform(v)
        );
        return v;
    }
    return Unquote_Quoted_Cell(As_Element(v));  // asserts that it's quoted
}

#define Unlift_Cell_No_Decay(v) \
    Unlift_Cell_No_Decay_Core(Possibly_Unstable(v))

INLINE Stable* Known_Stable_Unlift_Cell_Core(Stable* v) {
    assume (
      Unlift_Cell_No_Decay(u_cast(Value*, v))
    );
    Assert_Cell_Stable(v);
    return v;
}

#define Known_Stable_Unlift_Cell(v) \
    Known_Stable_Unlift_Cell_Core(Possibly_Antiform(v))

INLINE Dual* Copy_Lifted_Cell_Untracked(Init(Dual) out, const Value* v)
{
    Copy_Cell_Core_Untracked(out, v, CELL_MASK_COPY);
    return Lift_Cell(u_cast(Value*, out));
}

#define Copy_Lifted_Cell(out,v) \
    MAYBE_TRACK(Copy_Lifted_Cell_Untracked((out), (v)))

INLINE Dual* Copy_Plain_Cell_Untracked(Init(Dual) out, const Cell* cell) {
    Copy_Cell_Core_Untracked(out, cell, CELL_MASK_COPY);
    Clear_Cell_Quotes_And_Quasi(out);
    return out;
}

#define Copy_Plain_Cell(out,v) \
    MAYBE_TRACK(Copy_Plain_Cell_Untracked((out), (v)))
