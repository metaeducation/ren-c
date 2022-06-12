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


inline static OPT_SYMID VAL_WORD_ID(noquote(const Cell*) v) {
    assert(PG_Symbol_Canons[0].leader.bits == SERIES_FLAG_FREE);
    return ID_OF_SYMBOL(VAL_WORD_SYMBOL(v));
}

inline static void INIT_VAL_WORD_INDEX(Cell *v, REBLEN i) {
    assert(ANY_WORDLIKE(v));
    VAL_WORD_INDEX_U32(v) = i;
}

inline static REBVAL *Init_Any_Word_Untracked(
    Cell *out,
    enum Reb_Kind kind,
    const Symbol *sym
){
    Reset_Cell_Header_Untracked(out, kind, CELL_FLAG_FIRST_IS_NODE);
    VAL_WORD_INDEX_U32(out) = 0;
    mutable_BINDING(out) = nullptr;
    INIT_VAL_WORD_SYMBOL(out, sym);

    return cast(REBVAL*, out);
}

#define Init_Any_Word(out,kind,spelling) \
    Init_Any_Word_Untracked(TRACK(out), (kind), (spelling))

#define Init_Word(out,str)          Init_Any_Word((out), REB_WORD, (str))
#define Init_Get_Word(out,str)      Init_Any_Word((out), REB_GET_WORD, (str))
#define Init_Set_Word(out,str)      Init_Any_Word((out), REB_SET_WORD, (str))
#define Init_Meta_Word(out,str)     Init_Any_Word((out), REB_META_WORD, (str))

inline static REBVAL *Init_Any_Word_Bound_Untracked(
    Cell *out,
    enum Reb_Kind type,
    REBARR *binding,  // spelling determined by linked-to thing
    const Symbol *symbol,
    REBLEN index  // must be 1 if LET patch (INDEX_ATTACHED)
){
    Reset_Cell_Header_Untracked(out, type, CELL_FLAG_FIRST_IS_NODE);
    mutable_BINDING(out) = binding;
    VAL_WORD_INDEX_U32(out) = index;
    INIT_VAL_WORD_SYMBOL(out, symbol);

    if (IS_VARLIST(binding)) {
        if (CTX_TYPE(CTX(binding)) == REB_MODULE)
            assert(index == INDEX_ATTACHED);
        else
            assert(symbol == *CTX_KEY(CTX(binding), index));
    }
    else {
        assert(GET_SUBCLASS_FLAG(PATCH, binding, LET));
        assert(index == INDEX_PATCHED);
        assert(symbol == INODE(PatchSymbol, binding));
    }

    return cast(REBVAL*, out);
}

#define Init_Any_Word_Bound(out,type,context,symbol,index) \
    Init_Any_Word_Bound_Untracked(TRACK(out), \
            (type), CTX_VARLIST(context), (symbol), (index))

inline static REBVAL *Init_Any_Word_Patched(  // e.g. LET or MODULE! var
    Cell *out,
    enum Reb_Kind type,
    REBARR *patch
){
    return Init_Any_Word_Bound_Untracked(
        out,
        type,
        patch,
        INODE(PatchSymbol, patch),
        INDEX_PATCHED
    );
}

#define Init_Any_Word_Attached(out,type,module,symbol) \
    Init_Any_Word_Bound_Untracked(TRACK(out), \
            (type), (symbol), CTX_VARLIST(module), INDEX_ATTACHED)

// Helper calls strsize() so you can more easily use literals at callsite.
// (Better to call Intern_UTF8_Managed() with the size if you know it.)
//
inline static const REBSTR *Intern_Unsized_Managed(const char *utf8)
  { return Intern_UTF8_Managed(cb_cast(utf8), strsize(utf8)); }


// It's fundamental to PARSE to recognize `|` and skip ahead to it to the end.
// The debug build has enough checks on things like VAL_WORD_SYMBOL() that
// it adds up when you already tested someting IS_WORD().  This reaches a
// bit lower level to try and still have protections but speed up some--and
// since there's no inlining in the debug build, FETCH_TO_BAR_OR_END=>macro
//
// !!! The quick check that was here was undermined by words no longer always
// storing their symbols in the word; this will likely have to hit a keylist.
//
inline static bool IS_BAR(const Cell *v) {
    return VAL_TYPE_UNCHECKED(v) == REB_WORD
        and VAL_WORD_SYMBOL(v) == Canon(BAR_1);  // caseless | always canon
}

inline static bool IS_BAR_BAR(const Cell *v) {
    return VAL_TYPE_UNCHECKED(v) == REB_WORD
        and VAL_WORD_SYMBOL(v) == Canon(_B_B);  // caseless || always canon
}

inline static bool IS_META(const Cell *v) {
    if (not IS_WORD(v))
        return false;
    return VAL_WORD_SYMBOL(v) == Canon(CARET_1);
}

inline static bool IS_THE(const Cell *v) {
    if (not IS_WORD(v))
        return false;
    return VAL_WORD_SYMBOL(v) == Canon(AT_1);
}
