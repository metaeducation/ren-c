//
//  File: %cell-word.h
//  Summary: {Definitions for the ANY-WORD? Datatypes}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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

INLINE bool Any_Wordlike(const Cell* v) {
    // called by core code, sacrifice Ensure_Readable() checks
    if (Any_Word_Kind(Cell_Heart_Unchecked(v)))
        return true;
    if (not Any_Sequence_Kind(Cell_Heart_Unchecked(v)))
        return false;
    if (Not_Cell_Flag_Unchecked(v, FIRST_IS_NODE))
        return false;
    const Node* node1 = Cell_Node1(v);
    if (Is_Node_A_Cell(node1))
        return false;
    return Stub_Flavor(u_cast(const Flex*, node1)) == FLAVOR_SYMBOL;
}

INLINE void Tweak_Cell_Word_Symbol(Cell* v, const Symbol* symbol)
  { Tweak_Cell_Node1(v, symbol); }

INLINE const Symbol* Cell_Word_Symbol(const Cell* cell) {
    assert(Any_Wordlike(cell));  // no _UNCHECKED variant :-(
    return cast(Symbol*, Cell_Node1(cell));
}

#define Cell_Word_Id(v) \
    Symbol_Id(Cell_Word_Symbol(v))

// Use large indices to avoid confusion with 0 (reserved for unbound) and
// to avoid confusing with actual indices into objects.
//
#define INDEX_PATCHED (INT32_MAX - 1)  // directly points at variable patch
#define INDEX_ATTACHED INT32_MAX  // lazy creation of module variables

#define CELL_WORD_INDEX_I32(v)         PAYLOAD(Any, (v)).second.i32

INLINE void Tweak_Cell_Word_Index(Cell* v, REBINT i) {
    assert(Any_Wordlike(v));
    assert(i != 0);
    CELL_WORD_INDEX_I32(v) = i;
}

INLINE Element* Init_Any_Word_Untracked(
    Sink(Element*) out,
    Heart heart,
    const Symbol* sym
){
    assert(Any_Word_Kind(heart));
    Freshen_Cell_Untracked(out);
    out->header.bits |= (
        NODE_FLAG_NODE | NODE_FLAG_CELL
            | FLAG_HEART_BYTE(heart) | FLAG_QUOTE_BYTE(NOQUOTE_1)
            | CELL_FLAG_FIRST_IS_NODE
    );
    CELL_WORD_INDEX_I32(out) = 0;
    BINDING(out) = nullptr;
    Tweak_Cell_Word_Symbol(out, sym);
    return out;
}

#define Init_Any_Word(out,heart,spelling) \
    TRACK(Init_Any_Word_Untracked((out), (heart), (spelling)))

#define Init_Word(out,str)          Init_Any_Word((out), REB_WORD, (str))
#define Init_Get_Word(out,str)      Init_Any_Word((out), REB_GET_WORD, (str))
#define Init_Set_Word(out,str)      Init_Any_Word((out), REB_SET_WORD, (str))
#define Init_Meta_Word(out,str)     Init_Any_Word((out), REB_META_WORD, (str))

INLINE Value* Init_Any_Word_Bound_Untracked(
    Sink(Element*) out,
    Heart heart,
    const Symbol* symbol,
    Stub* binding,  // spelling determined by linked-to thing
    REBLEN index  // must be INDEX_PATCHED if LET patch
){
    assert(Any_Word_Kind(heart));
    assert(index != 0);
    Reset_Cell_Header_Untracked(
        out,
        FLAG_HEART_BYTE(heart) | CELL_FLAG_FIRST_IS_NODE
    );
    Tweak_Cell_Word_Symbol(out, symbol);
    CELL_WORD_INDEX_I32(out) = index;
    BINDING(out) = binding;

    if (Is_Stub_Varlist(binding)) {
        assert(CTX_TYPE(cast(Context*, binding)) != REB_MODULE);  // must patch
        assert(symbol == *CTX_KEY(cast(Context*, binding), index));
    }
    else {
        assert(Is_Stub_Let(binding) or Is_Stub_Patch(binding));
        assert(index == INDEX_PATCHED);
        assert(symbol == INODE(LetSymbol, binding));
    }

    return out;
}

#define Init_Any_Word_Bound(out,heart,symbol,context,index) \
    TRACK(Init_Any_Word_Bound_Untracked((out), \
            (heart), (symbol), CTX_VARLIST(context), (index)))

#define Init_Quasi_Word(out,label) \
    TRACK(Coerce_To_Quasiform( \
        Init_Any_Word_Untracked((out), REB_WORD, (label))))

#define Init_Anti_Word_Untracked(out,label) \
    Coerce_To_Stable_Antiform(Init_Any_Word_Untracked((out), REB_WORD, (label)))

#define Init_Anti_Word(out,label) \
    TRACK(Init_Anti_Word_Untracked((out), (label)))


// Helper calls strsize() so you can more easily use literals at callsite.
// (Better to call Intern_UTF8_Managed() with the size if you know it.)
//
INLINE const String* Intern_Unsized_Managed(const char *utf8)
  { return Intern_UTF8_Managed(cb_cast(utf8), strsize(utf8)); }


// It's fundamental to PARSE to recognize `|` and skip ahead to it to the end.
// The debug build has enough checks on things like Cell_Word_Symbol() that
// it adds up when you already tested someting Is_Word().  This reaches a
// bit lower level to try and still have protections but speed up some--and
// since there's no inlining in the debug build, FETCH_TO_BAR_OR_END=>macro
//
// !!! The quick check that was here was undermined by words no longer always
// storing their symbols in the word; this will likely have to hit a keylist.
//
INLINE bool Is_Bar(const Value* v) {
    return (
        HEART_BYTE(v) == REB_WORD
        and QUOTE_BYTE(v) == NOQUOTE_1
        and Cell_Word_Symbol(v) == Canon(BAR_1)  // caseless | always canon
    );
}

INLINE bool Is_Bar_Bar(const Atom* v) {
    return (
        HEART_BYTE(v) == REB_WORD
        and QUOTE_BYTE(v) == NOQUOTE_1
        and Cell_Word_Symbol(v) == Canon(_B_B)  // caseless || always canon
    );
}

INLINE bool Is_Anti_Word_With_Id(const Atom* v, SymId id) {
    assert(id != 0);
    if (not Is_Keyword(v))
        return false;
    return id == Cell_Word_Id(v);
}

INLINE bool Is_Quasi_Word_With_Id(const Value* v, SymId id) {
    assert(id != 0);
    if (not Is_Quasi_Word(v))
        return false;
    return id == Cell_Word_Id(v);
}

INLINE bool Is_Word_With_Id(const Value* v, SymId id) {
    assert(id != 0);
    if (not Is_Word(v))
        return false;
    return id == Cell_Word_Id(v);
}
