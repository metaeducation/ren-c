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
    // called by core code, sacrifice Ensure_Readable() checks
    if (Unchecked_Heart_Of(v) == TYPE_WORD)
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
    return c_cast(Symbol*, WORDLIKE_PAYLOAD_1_SYMBOL_BASE(c));
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
    Set_Cell_Flag(v, DONT_MARK_PAYLOAD_2);
}

INLINE void Tweak_Word_Stub(const Cell* v, Stub* stub) {
    assert(Is_Cell_Wordlike(v));
    assert(Is_Stub_Let(stub) or Is_Stub_Patch(stub));
    m_cast(Cell*, v)->payload.split.two.base = stub;
    Clear_Cell_Flag(v, DONT_MARK_PAYLOAD_2);
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
            | FLAG_HEART(TYPE_WORD)
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

INLINE Element* Init_Word_Untracked(
    Init(Element) out,
    Flags flags,
    const Symbol* symbol
){
    Reset_Cell_Header(out,
        FLAG_HEART(TYPE_WORD)
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
    TRACK(Init_Word_Untracked((out), FLAG_LIFT_BYTE(NOQUOTE_2), (str)))

#define Init_Quasi_Word(out,symbol) \
    TRACK(Init_Word_Untracked((out), FLAG_LIFT_BYTE(QUASIFORM_3), (symbol)))

INLINE Element* Init_Word_Bound_Untracked(
    Sink(Element) out,
    const Symbol* symbol,
    Context* binding
){
    Reset_Cell_Header_Noquote(
        out,
        FLAG_HEART(TYPE_WORD)
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
INLINE bool Any_Word(const Value* v)
  { return Any_Fundamental(v) and Heart_Of(v) == TYPE_WORD; }


// Helper calls strsize() so you can more easily use literals at callsite.
// (Better to call Intern_UTF8_Managed() with the size if you know it.)
//
INLINE const Strand* Intern_Unsized_Managed(const char *utf8)
  { return Intern_UTF8_Managed(cb_cast(utf8), strsize(utf8)); }


// It's fundamental to PARSE to recognize `|` and skip ahead to it to the end.
// The checked build has enough checks on things like Word_Symbol() that
// it adds up when you already tested someting Is_Word().  This reaches a
// bit lower level to try and still have protections but speed up some--and
// since there's no inlining in the checked build, FETCH_TO_BAR_OR_END=>macro
//
// !!! The quick check that was here was undermined by words no longer always
// storing their symbols in the word; this will likely have to hit a keylist.
//
INLINE bool Is_Bar(const Value* v) {
    return (
        Heart_Of(v) == TYPE_WORD
        and LIFT_BYTE(v) == NOQUOTE_2
        and Word_Symbol(v) == CANON(BAR_1)  // caseless | always canon
    );
}

INLINE bool Is_Bar_Bar(const Atom* v) {
    return (
        Heart_Of(v) == TYPE_WORD
        and LIFT_BYTE(v) == NOQUOTE_2
        and Word_Symbol(v) == CANON(_B_B)  // caseless || always canon
    );
}

INLINE bool Is_Anti_Word_With_Id(const Value* v, SymId id) {
    assert(id != SYM_0_constexpr);
    if (not Is_Keyword(v))
        return false;
    return id == Word_Id(v);
}

INLINE bool Is_Quasi_Word_With_Id(const Value* v, SymId id) {
    assert(id != SYM_0_constexpr);
    if (not Is_Quasi_Word(v))
        return false;
    return id == Word_Id(v);
}

INLINE bool Is_Word_With_Id(const Value* v, SymId id) {
    assert(id != SYM_0_constexpr);
    if (not Is_Word(v))
        return false;
    return id == Word_Id(v);
}


//=//// <end> SIGNALING WITH UNSET (_ dual) ///////////////////////////////=//
//
// Special handling is required in order to allow a kind of "light variadic"
// form, where a parameter can be missing.
//
// This macro helps keep track of those places in the source that are the
// implementation of the "unset due to end" behavior.
//

#define Is_Dual_Word_Unset_Signal(dual) \
    Is_Word_With_Id((dual), SYM__PUNSET_P)

#define Init_Dual_Word_Unset_Signal(dual) \
    Init_Word((dual), CANON(_PUNSET_P))

INLINE Slot* Init_Dual_Unset(Cell* slot) {
    Init_Dual_Word_Unset_Signal(slot);
    LIFT_BYTE(slot) = DUAL_0;
    return u_cast(Slot*, slot);
}

INLINE bool Is_Dual_Unset(const Cell* cell) {
    if (LIFT_BYTE(cell) != DUAL_0)
        return false;
    return Word_Id(cell) == SYM__PUNSET_P;
}

INLINE Atom* Init_Unset_Due_To_End(Init(Atom) out) {
    Init_Dual_Word_Unset_Signal(out);
    LIFT_BYTE(out) = DUAL_0;
    return out;
}

#define Is_Endlike_Unset(cell) \
    Is_Dual_Unset(cell)


//=//// *BLACKHOLE* DUAL SIGNAL ///////////////////////////////////////////-//
//
// This is what slots are set to when you do things like:
//
//    for-each _ [1 2 3] [...]
//

#define Is_Dual_Word_Blackhole_Signal(dual) \
    Is_Word_With_Id((dual), SYM__PBLACKHOLE_P)

#define Init_Dual_Word_Blackhole_Signal(dual) \
    Init_Word((dual), CANON(_PBLACKHOLE_P))

INLINE bool Is_Blackhole_Slot(const Slot* slot) {
    if (LIFT_BYTE(slot) != DUAL_0)
        return false;
    if (KIND_BYTE(slot) != TYPE_WORD)
        return false;
    return Word_Id(slot) == SYM__PBLACKHOLE_P;
}

INLINE Slot* Init_Blackhole_Slot(Init(Slot) out) {
    Init_Dual_Word_Blackhole_Signal(out);
    LIFT_BYTE(out) = DUAL_0;
    return out;
}
