//
//  file: %cell-word.h
//  summary: "Definitions for the ANY-WORD? Cells"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2024 Ren-C Open Source Contributors
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
// Words are the fundamental symbolic concept of Rebol.  Rather than storing a
// pointer to a mutable string, words store a pointer to a read-only symbol
// (see %sys-symbol.h) that can be quickly looked up and compared.
//
// Words can act as a variable when bound specifically to a context
// (see %sys-context.h) or bound relatively to an action (see %sys-action.h).
//
// For routines that manage binding, see %sys-bind.h.
//

#define WORDLIKE_PAYLOAD_1_SYMBOL_BASE  SERIESLIKE_PAYLOAD_1_BASE

INLINE bool Is_Cell_Wordlike(const Cell* v) {
    // called by core code, sacrifice Readable_Cell() checks
    if (Unchecked_Heart_Of(v) == HEART_WORD)
        return true;
    if (not Any_Sequence_Type(Unchecked_Heart_Of(v)))
        return false;
    if (not Cell_Payload_1_Needs_Mark(v))
        return false;
    const Base* payload1 = SERIESLIKE_PAYLOAD_1_BASE(v);
    if (Is_Base_A_Cell(payload1))
        return false;
    return Stub_Flavor(u_cast(const Flex*, payload1)) == FLAVOR_SYMBOL;
}

INLINE const Symbol* Word_Symbol(const Cell* c) {
    assert(Is_Cell_Wordlike(c));
    return cast(Symbol*, WORDLIKE_PAYLOAD_1_SYMBOL_BASE(c));
}

#define Word_Id(v) \
    Symbol_Id(Word_Symbol(v))

// Use large indices to avoid confusion with 0 (reserved for unbound) and
// to avoid confusing with actual indices into objects.
//
#define INDEX_PATCHED (INT32_MAX - 1)  // directly points at variable patch

#define CELL_WORD_INDEX_I32(c)         (c)->payload.split.two.i32

INLINE void Tweak_Word_Index(const Cell* v, Index i) {
    assert(Is_Cell_Wordlike(v));
    assert(i != 0);
    CELL_WORD_INDEX_I32(m_cast(Cell*, v)) = i;
    Set_Cell_Flag(m_cast(Cell*, v), DONT_MARK_PAYLOAD_2);
}

INLINE void Tweak_Word_Stub(const Cell* v, Stub* stub) {
    assert(Is_Cell_Wordlike(v));
    assert(Is_Stub_Let(stub) or Is_Stub_Patch(stub));
    m_cast(Cell*, v)->payload.split.two.base = stub;
    Clear_Cell_Flag(m_cast(Cell*, v), DONT_MARK_PAYLOAD_2);
}

INLINE Cell* Blit_Word_Untracked(
    Cell* out,
    Flags flags,
    const Symbol* sym
){
  #if DEBUG_POISON_UNINITIALIZED_CELLS
    assert(Is_Cell_Poisoned(out) or Is_Cell_Erased(out));
  #endif
    out->header.bits = (  // NOTE: `=` and not `|=` ... full overwrite
        BASE_FLAG_BASE | BASE_FLAG_CELL  // must include base flags
            | FLAG_HEART(HEART_WORD)
            | (( flags ))
            | (not CELL_FLAG_DONT_MARK_PAYLOAD_1)  // symbol needs mark
            | CELL_FLAG_DONT_MARK_PAYLOAD_2  // index shouldn't be marked
    );
    CELL_WORD_INDEX_I32(out) = 0;  // !!! hint used in special cases
    WORDLIKE_PAYLOAD_1_SYMBOL_BASE(out) = m_cast(Symbol*, sym);
    UNNECESSARY(Tweak_Cell_Binding(out, UNBOUND));  // don't need checks...
    out->extra.base = UNBOUND;  // ...just assign directly, always valid
    return out;
}

INLINE Dual* Init_Word_Untracked(
    Init(Dual) out,
    Flags flags,
    const Symbol* symbol
){
    Reset_Cell_Header(out,
        FLAG_HEART(HEART_WORD)
            | (( flags ))
            | (not CELL_FLAG_DONT_MARK_PAYLOAD_1)  // symbol needs mark
            | CELL_FLAG_DONT_MARK_PAYLOAD_2  // index shouldn't be marked
    );
    CELL_WORD_INDEX_I32(out) = 0;  // !!! hint used in special cases
    WORDLIKE_PAYLOAD_1_SYMBOL_BASE(out) = m_cast(Symbol*, symbol);
    UNNECESSARY(Tweak_Cell_Binding(out, UNBOUND));  // don't need checks...
    out->extra.base = UNBOUND;  // ...just assign directly, always valid
    return out;
}

#define Init_Word(out,str) \
    TRACK(Init_Word_Untracked((out), FLAG_LIFT(TYPE_WORD), (str)))

#define Init_Quasi_Word(out,symbol) \
    TRACK(Init_Word_Untracked((out), FLAG_LIFT(TYPE_QUASIFORM), (symbol)))

INLINE Dual* Init_Word_Bound_Untracked(
    Sink(Dual) out,
    const Symbol* symbol,
    Context* binding
){
    Reset_Cell_Header(
        out,
        FLAG_HEART_AND_LIFT(HEART_WORD)
            | (not CELL_FLAG_DONT_MARK_PAYLOAD_1)  // symbol needs mark
            | CELL_FLAG_DONT_MARK_PAYLOAD_2  // index shouldn't be marked
    );
    WORDLIKE_PAYLOAD_1_SYMBOL_BASE(out) = m_cast(Symbol*, symbol);
    CELL_WORD_INDEX_I32(out) = 0;  // !!! hint used in special cases
    Tweak_Cell_Binding(out, binding);  // validates if DEBUG_CHECK_BINDING

    return out;
}

#define Init_Word_Bound(out,symbol,context) \
    TRACK(Init_Word_Bound_Untracked((out), (symbol), (context)))


// !!! It used to be that ANY-WORD? included sigilized words.  That is no
// longer a fundamental type, since a sigilized word is e.g. METAFORM!.  This
// is a placeholder to try and get things compiling.
//
INLINE bool Any_Word(const Stable* v)
  { return Any_Fundamental(v) and Heart_Of(v) == HEART_WORD; }


// Helper calls strsize() so you can more easily use literals at callsite.
// (Better to call Intern_Symbol() with the size if you know it.)
//
INLINE Result(Managed(const Symbol*)) Intern_Unsized_Symbol(const char *bp)
  { return Intern_Symbol(b_cast(bp), strsize(bp)); }



//=//// WORD ID CHECKS ////////////////////////////////////////////////////=//
//
// Because WORD! doesn't change its Symbol* over the lifetime of the cell, we
// could theoretically do things like store the SymId in the extra 32-bits
// of the header on 64-bit builds (for instance) and be able to check for
// certain word identities by just looking at the header.
//
// In any case, it's better to test if a word has an ID by using these checks
// rather than extracting the Symbol* and asking it.
//

INLINE bool Is_Word_With_Id_Core(const Cell* v, Type type, SymId id) {
    assert(id != SYM_0_constexpr);
    if (not Cell_Has_Lift_Sigil_Heart(v, type, SIGIL_0, HEART_WORD))
        return false;
    return id == Word_Id(v);  // is CANON(id) == Word_Symbol(v) faster?
}

#define Is_Word_With_Id(v,id) \
    Is_Word_With_Id_Core(Known_Stable(v), TYPE_WORD, (id))

#define Is_Quasi_Word_With_Id(v,id) \
    Is_Word_With_Id_Core(Known_Stable(v), TYPE_QUASIFORM, (id))


//=//// '| AND '|| WORD CHECKS ////////////////////////////////////////////=//
//
// Early micro-optimizations of PARSE3 noticed that recognizing `|` and
// skipping to the end was actually one of the more expensive parts.  There
// were optimized tests for recognizing | and ||.
//
// However, the system has changed significantly since that time, and the
// performance concerns are different...not to mention that builtin symbol
// recognition is much faster.  These are now checked "normally".
//

#define Is_Bar(v) \
    Is_Word_With_Id(v, SYM_BAR_1)  // caseless | always canon

#define Is_Bar_Bar(v) \
    Is_Word_With_Id(v, SYM__B_B)  // caseless || always canon
