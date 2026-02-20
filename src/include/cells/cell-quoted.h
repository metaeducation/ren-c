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
// Cells reserve a byte in their header called the LIFT_BYTE().  The most
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
// But the LIFT_BYTE() is used to encode other states as well: some datatypes
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
    assert(LIFT_BYTE_RAW(v) < MIN_LIFTBYTE_ANTIFORM);
    return (LIFT_BYTE(v) - NOQUOTE_63) >> 1;
}

INLINE Count Quotes_From_Lift_Byte(LiftByte lift_byte) {
    assert(lift_byte < MIN_LIFTBYTE_ANTIFORM);
    return (lift_byte - NOQUOTE_63) >> 1;
}

#define Is_Unquoted(v) \
    (LIFT_BYTE(Ensure_Readable(Known_Stable(v))) == NOQUOTE_63)

#define Is_Possibly_Unstable_Value_Quoted(v) \
    (LIFT_BYTE(Ensure_Readable(v)) >= ONEQUOTE_NONQUASI_65)

#define Is_Quoted(v) \
    Is_Possibly_Unstable_Value_Quoted(Known_Stable(v))

#define Any_Fundamental(v) \
    (LIFT_BYTE(Ensure_Readable(Known_Stable(v))) == NOQUOTE_63)

#define Is_Quoted_Form_Of(heartname, v) \
    Cell_Has_Lift_Sigil_Heart(Known_Stable(v), \
        ONEQUOTE_NONQUASI_65, SIGIL_0, TYPE_##heartname)


// Turns X into 'X, or '''[1 + 2] into '''''(1 + 2), etc.
//
INLINE Element* Quotify_Depth(Element* elem, Count depth) {
    assert(LIFT_BYTE_RAW(elem) < MIN_LIFTBYTE_ANTIFORM);

    if (depth == 0)
        return elem;

    if (Quotes_Of(elem) + depth >  MAX_QUOTE_DEPTH)
        panic ("Quoting Depth of 126 Exceeded");

    LIFT_BYTE_RAW(elem) += Quote_Shift(depth);
    return elem;
}


// Turns 'X into X, or '''''[1 + 2] into '''(1 + 2), etc.
//
INLINE Element* Unquotify_Depth(Element* elem, Count depth) {
    assert(LIFT_BYTE_RAW(elem) < MIN_LIFTBYTE_ANTIFORM);

    if (depth == 0)
        return elem;

    if (depth > Quotes_Of(elem))
        panic ("Attempt to set quoting level of value to less than 0");

    LIFT_BYTE_RAW(elem) -= Quote_Shift(depth);
    return elem;
}

#define Quote_Cell(elem)      Quotify_Depth((elem), 1)
#define Unquote_Cell(elem)    Unquotify_Depth((elem), 1)

INLINE Count Noquotify(Element* elem) {
    Count depth = Quotes_Of(elem);
    if (LIFT_BYTE_RAW(elem) & NONQUASI_BIT)
        LIFT_BYTE_RAW(elem) = NOQUOTE_63;
    else
        LIFT_BYTE_RAW(elem) = QUASIFORM_64;  // already quasi
    return depth;
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
        assert(LIFT_BYTE_RAW(v) != BEDROCK_255);
        return LIFT_BYTE(Ensure_Readable(v)) >= MIN_LIFTBYTE_ANTIFORM;
    }

    INLINE bool Is_Antiform(const Element* v) = delete;
#else
    #define Is_Antiform(v) \
        (LIFT_BYTE(Ensure_Readable(v)) >= MIN_LIFTBYTE_ANTIFORM)
#endif

#define Not_Antiform(v) (not Is_Antiform(v))

#undef Any_Antiform  // Is_Antiform() faster than auto-generated macro [1]

#define Is_Lifted_Antiform(v) \
    (LIFT_BYTE(Ensure_Readable(v)) == QUASIFORM_64)


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
//    "slower" (have to test >= MIN_LIFTBYTE_ANTIFORM v. simple equality), but
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
        (LIFT_BYTE(v) <= MAX_LIFTBYTE_STABLE)  // it's really fast! [1]

    #define Is_Antiform_Stable  Is_Cell_Stable  // equivalent [2]
#else
    INLINE bool Is_Cell_Stable_Core(const Value* v) {  // careful checks...
        possibly(not Is_Antiform(v));  // general check for any Value

        Assert_Cell_Readable(v);
        assert(LIFT_BYTE_RAW(v) != BEDROCK_255);

        bool stable = (LIFT_BYTE_RAW(v) <= MAX_LIFTBYTE_STABLE);

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
        unnecessary(Ensure_Readable(a));  // Is_Antiform() checked readable
        assert(LIFT_BYTE(a) >= MIN_LIFTBYTE_ANTIFORM);
        impossible(0 != (a->header.bits & CELL_MASK_SIGIL));
        return Is_Cell_Stable(a);
    }

    #define Is_Antiform_Stable(v) \
        Is_Antiform_Stable_Core(Possibly_Unstable(v))
#endif

#define Not_Cell_Stable(v)  (not Is_Cell_Stable(v))
#define Not_Antiform_Stable(v)  (not Is_Antiform_Stable(v))

INLINE bool Is_Lifted_Unstable_Antiform(const Value* v) {  // costs more [3]
    unnecessary(Ensure_Readable(v));  // assume Is_Antiform() checked readable

    if (LIFT_BYTE(v) != QUASIFORM_64)
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
    CONSTABLE(Stable*) v = m_cast(Stable*, Ensure_Readable(v_));
    if (Is_Antiform(v))
        return nullptr;
    return As_Element(v);
}

MUTABLE_IF_C(Element*, INLINE) Ensure_Element(CONST_IF_C(Value*) cell) {
    CONSTABLE(Value*) v = m_cast(Value*, cell);
    if (LIFT_BYTE(v) >= MIN_LIFTBYTE_ANTIFORM)
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
    (LIFT_BYTE(Ensure_Readable(Known_Stable(v))) == QUASIFORM_64)

INLINE Element* Unquasify(Element* elem) {
    assert(LIFT_BYTE(elem) == QUASIFORM_64);
    LIFT_BYTE(elem) = NOQUOTE_63;
    return elem;
}

INLINE Element* Quasify_Isotopic_Fundamental(Element* elem) {
    assert(Any_Isotopic_Type(Heart_Of(elem)));
    assert(LIFT_BYTE(elem) == NOQUOTE_63);
    LIFT_BYTE_RAW(elem) = QUASIFORM_64;
    return elem;
}

INLINE Element* Quasify_Antiform(Exact(Stable*) v) {
    assert(Is_Antiform(v));
    LIFT_BYTE_RAW(v) = QUASIFORM_64;  // all antiforms can be quasi
    return u_cast(Element*, v);
}

INLINE Element* Reify_If_Antiform(Value* v) {
    if (LIFT_BYTE(v) < MIN_LIFTBYTE_ANTIFORM)
        return As_Element(v);
    assert(LIFT_BYTE_RAW(v) != BEDROCK_255);
    LIFT_BYTE_RAW(v) = QUASIFORM_64;  // all antiforms can become quasi
    return As_Element(v);
}


//=//// ANTIFORMIZE FUNDAMENTALS /////////////////////////////////////////=//
//
// !!! Warning: Internal use only, this bypasses typical checks: go through
// the Coerce_To_Antiform() function instead unless you know what you
// are doing.
//

INLINE void Antiformize_Unbound_Fundamental(Value* v, LiftByte lift_byte) {
    assert(Heart_Of(v) != HEART_WORD_SIGNIFYING_LOGIC);  // no LOGIC_IS_OKAY
    assert(LIFT_BYTE_RAW(v) == NOQUOTE_63);
    possibly(Not_Stable_Antiform_Heart(Heart_Of_Unsigiled_Isotopic(v)));
    if (Is_Bindable_Heart(Unchecked_Heart_Of(v)))
        assert(not Cell_Binding(v));
    LIFT_BYTE_RAW(v) = lift_byte;
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
    if (LIFT_BYTE_RAW(v) < QUASIFORM_64)
        return true;  // fundamental
    if (LIFT_BYTE(v) >= MIN_LIFTBYTE_ANTIFORM)
        return true;  // antiform
    return false;  // quoted or quasi
}

#define Any_Lifted(v) \
    (not Not_Lifted(v))  // anti or fundamental

INLINE Dual* Lift_Cell(Value* v) {
    if (LIFT_BYTE_RAW(v) < MIN_LIFTBYTE_ANTIFORM)
        return As_Dual(Quote_Cell(As_Element(v)));  // non-antiform -> quoted

    assert(LIFT_BYTE_RAW(v) != BEDROCK_255);
    LIFT_BYTE_RAW(v) = QUASIFORM_64;  // both unstable and stable become quasi
    return As_Dual(v);
}

INLINE Result(Value*) Unlift_Cell_No_Decay_Core(Value* v) {
    if (LIFT_BYTE_RAW(v) == QUASIFORM_64) {
        trap (
          Coerce_To_Antiform(v)
        );
        return v;
    }
    unnecessary(assert(LIFT_BYTE_RAW(v) < MIN_LIFTBYTE_ANTIFORM));
    return Unquote_Cell(As_Element(v));  // asserts that it's quoted
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
    LIFT_BYTE(out) = NOQUOTE_63;
    return out;
}

#define Copy_Plain_Cell(out,v) \
    MAYBE_TRACK(Copy_Plain_Cell_Untracked((out), (v)))
