//
//  File: %cell-word.h
//  Summary: {Definitions for the ANY-WORD? Datatypes}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Ren-C Open Source Contributors
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
    // called by core code, sacrifice READABLE() checks
    if (Any_Word_Kind(Cell_Heart_Unchecked(v)))
        return true;
    if (not Any_Sequence_Kind(Cell_Heart_Unchecked(v)))
        return false;
    if (Not_Cell_Flag_Unchecked(v, FIRST_IS_NODE))
        return false;
    const Node* node1 = Cell_Node1(v);
    if (Is_Node_A_Cell(node1))
        return false;
    return Series_Flavor(u_cast(const Series*, node1)) == FLAVOR_SYMBOL;
}

INLINE void INIT_CELL_WORD_SYMBOL(Cell* v, const Symbol* symbol)
  { Init_Cell_Node1(v, symbol); }

INLINE const Symbol* Cell_Word_Symbol(const Cell* cell) {
    assert(Any_Wordlike(cell));  // no _UNCHECKED variant :-(
    return cast(Symbol*, Cell_Node1(cell));
}

#define Cell_Word_Id(v) \
    Symbol_Id(Cell_Word_Symbol(v))

INLINE void INIT_VAL_WORD_INDEX(Cell* v, REBINT i) {
    assert(Any_Wordlike(v));
    assert(i != 0);
    VAL_WORD_INDEX_I32(v) = i;
}

INLINE Element* Init_Any_Word_Untracked(
    Sink(Element*) out,
    Heart heart,
    const Symbol* sym,
    Byte quote_byte
){
    assert(Any_Word_Kind(heart));
    FRESHEN_CELL(out);
    out->header.bits |= (
        NODE_FLAG_NODE | NODE_FLAG_CELL
            | FLAG_HEART_BYTE(heart) | FLAG_QUOTE_BYTE(quote_byte)
            | CELL_FLAG_FIRST_IS_NODE
    );
    VAL_WORD_INDEX_I32(out) = 0;
    BINDING(out) = nullptr;
    INIT_CELL_WORD_SYMBOL(out, sym);
    return out;
}

#define Init_Any_Word(out,heart,spelling) \
    TRACK(Init_Any_Word_Untracked((out), (heart), (spelling), NOQUOTE_1))

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
    Reset_Unquoted_Header_Untracked(
        out,
        FLAG_HEART_BYTE(heart) | CELL_FLAG_FIRST_IS_NODE
    );
    INIT_CELL_WORD_SYMBOL(out, symbol);
    VAL_WORD_INDEX_I32(out) = index;
    BINDING(out) = binding;

    if (IS_VARLIST(binding)) {
        assert(CTX_TYPE(cast(Context*, binding)) != REB_MODULE);  // must patch
        assert(symbol == *CTX_KEY(cast(Context*, binding), index));
    }
    else {
        assert(IS_LET(binding) or IS_PATCH(binding));
        assert(index == INDEX_PATCHED);
        assert(symbol == INODE(LetSymbol, binding));
    }

    return cast(Value*, out);
}

#define Init_Any_Word_Bound(out,heart,symbol,context,index) \
    TRACK(Init_Any_Word_Bound_Untracked((out), \
            (heart), (symbol), CTX_VARLIST(context), (index)))


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
INLINE bool IS_BAR(const Atom* v) {
    return VAL_TYPE_UNCHECKED(v) == REB_WORD
        and Cell_Word_Symbol(v) == Canon(BAR_1);  // caseless | always canon
}

INLINE bool IS_BAR_BAR(const Atom* v) {
    return VAL_TYPE_UNCHECKED(v) == REB_WORD
        and Cell_Word_Symbol(v) == Canon(_B_B);  // caseless || always canon
}

// !!! Temporary workaround for what was Is_Meta_Word() (now not its own type)
//
INLINE bool IS_QUOTED_WORD(const Atom* v) {
    return Cell_Num_Quotes(v) == 1
        and Cell_Heart(v) == REB_WORD;
}

INLINE bool Is_Anti_Word_With_Id(Need(const Value*) v, SymId id) {
    assert(id != 0);

    if (not Is_Antiword(v))
        return false;

    return id == Cell_Word_Id(v);
}

INLINE bool Is_Quasi_Word_With_Id(const Atom* v, SymId id) {
    assert(id != 0);

    if (not Is_Quasi_Word(v))
        return false;

    return id == Cell_Word_Id(v);
}
