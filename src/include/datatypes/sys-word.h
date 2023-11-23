//
//  File: %sys-word.h
//  Summary: {Definitions for the ANY-WORD! Datatypes}
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
// ANY-WORD! is the fundamental symbolic concept of Rebol.  Rather than
// storing a pointer to a mutable string, it stores a pointer to a read-only
// symbol (see %sys-symbol.h) that can be quickly looked up and compared.
//
// Words can act as a variable when bound specifically to a context
// (see %sys-context.h) or bound relatively to an action (see %sys-action.h).
//
// For routines that manage binding, see %sys-bind.h.
//

#define VAL_WORD_ID(v) \
    Symbol_Id(Cell_Word_Symbol(v))

INLINE void INIT_VAL_WORD_INDEX(Cell* v, REBLEN i) {
    assert(Any_Wordlike(v));
    assert(i != 0);
    VAL_WORD_INDEX_U32(v) = i;
}

INLINE REBVAL *Init_Any_Word_Untracked(
    Cell* out,
    enum Reb_Kind kind,
    const Symbol* sym,
    Byte quote_byte
){
    FRESHEN_CELL_EVIL_MACRO(out);
    out->header.bits |= (
        NODE_FLAG_NODE | NODE_FLAG_CELL
            | FLAG_HEART_BYTE(kind) | FLAG_QUOTE_BYTE(quote_byte)
            | CELL_FLAG_FIRST_IS_NODE
    );
    VAL_WORD_INDEX_U32(out) = 0;
    mutable_BINDING(out) = nullptr;
    INIT_Cell_Word_Symbol(out, sym);

    return cast(REBVAL*, out);
}

#define Init_Any_Word(out,kind,spelling) \
    TRACK(Init_Any_Word_Untracked((out), (kind), (spelling), UNQUOTED_1))

#define Init_Word(out,str)          Init_Any_Word((out), REB_WORD, (str))
#define Init_Get_Word(out,str)      Init_Any_Word((out), REB_GET_WORD, (str))
#define Init_Set_Word(out,str)      Init_Any_Word((out), REB_SET_WORD, (str))
#define Init_Meta_Word(out,str)     Init_Any_Word((out), REB_META_WORD, (str))

INLINE REBVAL *Init_Any_Word_Bound_Untracked(
    Cell* out,
    enum Reb_Kind type,
    const Symbol* symbol,
    Array* binding,  // spelling determined by linked-to thing
    REBLEN index  // must be 1 if LET patch (INDEX_ATTACHED)
){
    assert(index != 0);

    Reset_Unquoted_Header_Untracked(
        out,
        FLAG_HEART_BYTE(type) | CELL_FLAG_FIRST_IS_NODE
    );
    mutable_BINDING(out) = binding;
    VAL_WORD_INDEX_U32(out) = index;
    INIT_Cell_Word_Symbol(out, symbol);

    if (IS_VARLIST(binding)) {
        if (CTX_TYPE(cast(Context*, binding)) == REB_MODULE)
            assert(index == INDEX_ATTACHED);
        else
            assert(symbol == *CTX_KEY(cast(Context*, binding), index));
    }
    else {
        assert(IS_LET(binding) or IS_PATCH(binding));
        assert(index == INDEX_PATCHED);
        assert(symbol == INODE(LetSymbol, binding));
    }

    return cast(REBVAL*, out);
}

#define Init_Any_Word_Bound(out,type,symbol,context,index) \
    TRACK(Init_Any_Word_Bound_Untracked((out), \
            (type), (symbol), CTX_VARLIST(context), (index)))

INLINE REBVAL *Init_Any_Word_Patched(  // e.g. LET or MODULE! var
    Cell* out,
    enum Reb_Kind type,
    Array* patch
){
    return Init_Any_Word_Bound_Untracked(
        out,
        type,
        INODE(LetSymbol, patch),
        patch,
        INDEX_PATCHED
    );
}

#define Init_Any_Word_Attached(out,type,symbol,module) \
    TRACK(Init_Any_Word_Bound_Untracked((out), \
            (type), (symbol), CTX_VARLIST(module), INDEX_ATTACHED))

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
INLINE bool IS_BAR(const Cell* v) {
    return VAL_TYPE_UNCHECKED(v) == REB_WORD
        and Cell_Word_Symbol(v) == Canon(BAR_1);  // caseless | always canon
}

INLINE bool IS_BAR_BAR(const Cell* v) {
    return VAL_TYPE_UNCHECKED(v) == REB_WORD
        and Cell_Word_Symbol(v) == Canon(_B_B);  // caseless || always canon
}

INLINE bool IS_META(const Cell* v) {
    if (not Is_Word(v))
        return false;
    return Cell_Word_Symbol(v) == Canon(CARET_1);
}

INLINE bool IS_THE(const Cell* v) {
    if (not Is_Word(v))
        return false;
    return Cell_Word_Symbol(v) == Canon(AT_1);
}

// !!! Temporary workaround for what was Is_Meta_Word() (now not its own type)
//
INLINE bool IS_QUOTED_WORD(const Cell* v) {
    return Cell_Num_Quotes(v) == 1
        and Cell_Heart(v) == REB_WORD;
}


INLINE bool Is_Word_Isotope(const Cell* v) {
    ASSERT_CELL_READABLE_EVIL_MACRO(v);
    if (HEART_BYTE(v) != REB_WORD)
        return false;
    return QUOTE_BYTE(v) == ISOTOPE_0;
}

INLINE bool Is_Word_Isotope_With_Id(const Cell* v, SymId id) {
    assert(id != 0);

    if (not Is_Word_Isotope(v))
        return false;

    return id == VAL_WORD_ID(v);
}

INLINE bool Is_Quasi_Word(const Cell* v)
  { return Is_Quasi(v) and HEART_BYTE(v) == REB_WORD; }
