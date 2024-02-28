//
//  File: %sys-word.h
//  Summary: {Definitions for the ANY-WORD! Datatypes}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The ANY-WORD! is the fundamental symbolic concept of Rebol.  It is
// implemented as a Symbol UTF-8 string (see %sys-string.h), and can act as
// a variable when it is bound specifically to a context (see %sys-context.h)
// or when bound relatively to a function (see %sys-function.h).
//
// For routines that manage binding, see %sys-bind.h.
//
// !!! Today's words are different from ANY-STRING! values.  This is because
// they are interned (only one copy of the string data for all instances),
// read-only, use UTF-8 instead of a variable 1 or 2-bytes per character,
// and permit binding.  Ren-C intends to pare away these differences, perhaps
// even to the point of allowing mutable WORD!s and bindable STRING!s.  This
// is at the idea stage, but is evolving.
//

#ifdef NDEBUG
    #define WORD_FLAG(n) \
        FLAG_LEFT_BIT(TYPE_SPECIFIC_BIT + (n))
#else
    #define WORD_FLAG(n) \
        (FLAG_LEFT_BIT(TYPE_SPECIFIC_BIT + (n)) | FLAG_KIND_BYTE(REB_WORD))
#endif


INLINE bool IS_WORD_UNBOUND(const Cell* v) {
    assert(ANY_WORD(v));
    return v->extra.binding == nullptr;
}

#define IS_WORD_BOUND(v) \
    cast(bool, not IS_WORD_UNBOUND(v))

INLINE Symbol* Cell_Word_Symbol(const Cell* v) {
    assert(ANY_WORD(v));
    return v->payload.any_word.symbol;
}

INLINE Symbol* VAL_WORD_CANON(const Cell* v) {
    assert(ANY_WORD(v));
    return Canon_Symbol(v->payload.any_word.symbol);
}

// Some scenarios deliberately store canon symbols in words, to avoid
// needing to re-canonize them.  If you have one of those words, use this to
// add a check that your assumption about them is correct.
//
// Note that canon symbols can get GC'd, effectively changing the canon.
// But they won't if there are any words outstanding that hold that symbol,
// so this is a safe technique as long as these words are GC-mark-visible.
//
INLINE Symbol* VAL_STORED_CANON(const Cell* v) {
    assert(ANY_WORD(v));
    assert(GET_SER_INFO(v->payload.any_word.symbol, STRING_INFO_CANON));
    return v->payload.any_word.symbol;
}

INLINE Option(SymId) Cell_Word_Id(const Cell* v) {
    return Symbol_Id(v->payload.any_word.symbol);
}

INLINE REBCTX *VAL_WORD_CONTEXT(const Value* v) {
    assert(IS_WORD_BOUND(v));
    REBNOD *binding = VAL_BINDING(v);
    assert(
        GET_SER_FLAG(binding, NODE_FLAG_MANAGED)
        or IS_END(FRM(LINK(binding).keysource)->param) // not fulfilling
    );
    binding->header.bits |= NODE_FLAG_MANAGED; // !!! review managing needs
    return CTX(binding);
}

INLINE void INIT_WORD_INDEX(Cell* v, REBLEN i) {
  #if !defined(NDEBUG)
    INIT_WORD_INDEX_Extra_Checks_Debug(v, i); // not inline, needs FRM_PHASE()
  #endif
    v->payload.any_word.index = cast(REBINT, i);
}

INLINE REBLEN VAL_WORD_INDEX(const Cell* v) {
    assert(IS_WORD_BOUND(v));
    REBINT i = v->payload.any_word.index;
    assert(i > 0);
    return cast(REBLEN, i);
}

INLINE void Unbind_Any_Word(Cell* v) {
    INIT_BINDING(v, UNBOUND);
#if !defined(NDEBUG)
    v->payload.any_word.index = 0;
#endif
}

INLINE Value* Init_Any_Word(
    Cell* out,
    enum Reb_Kind kind,
    Symbol* symbol
){
    RESET_CELL(out, kind);
    out->payload.any_word.symbol = symbol;
    INIT_BINDING(out, UNBOUND);
  #if !defined(NDEBUG)
    out->payload.any_word.index = 0; // index not heeded if no binding
  #endif
    return KNOWN(out);
}

#define Init_Word(out,symbol) \
    Init_Any_Word((out), REB_WORD, (symbol))

#define Init_Get_Word(out,symbol) \
    Init_Any_Word((out), REB_GET_WORD, (symbol))

#define Init_Set_Word(out,symbol) \
    Init_Any_Word((out), REB_SET_WORD, (symbol))

#define Init_Lit_Word(out,symbol) \
    Init_Any_Word((out), REB_LIT_WORD, (symbol))

#define Init_Refinement(out,symbol) \
    Init_Any_Word((out), REB_REFINEMENT, (symbol))

#define Init_Issue(out,symbol) \
    Init_Any_Word((out), REB_ISSUE, (symbol))

// Initialize an ANY-WORD! type with a binding to a context.
//
INLINE Value* Init_Any_Word_Bound(
    Cell* out,
    enum Reb_Kind type,
    Symbol* symbol,
    REBCTX *context,
    REBLEN index
) {
    RESET_CELL(out, type);
    out->payload.any_word.symbol = symbol;
    INIT_BINDING(out, context);
    INIT_WORD_INDEX(out, index);
    return KNOWN(out);
}


// To make interfaces easier for some functions that take Symbol* strings,
// it can be useful to allow passing UTF-8 text, a Value* with an ANY-WORD!
// or ANY-STRING!, or just plain UTF-8 text.
//
// !!! Should NULLED_CELL or other arguments make anonymous symbols?
//
#if CPLUSPLUS_11
template<typename T>
INLINE Symbol* Intern(const T *p)
{
    static_assert(
        std::is_same<T, Value>::value
        or std::is_same<T, char>::value
        or std::is_same<T, Symbol>::value,
        "STR works on: char*, Value*, Symbol*"
    );
#else
INLINE Symbol* Intern(const void *p)
{
#endif
    switch (Detect_Rebol_Pointer(p)) {
    case DETECTED_AS_UTF8: {
        const char *utf8 = cast(const char*, p);
        return Intern_UTF8_Managed(cb_cast(utf8), strlen(utf8)); }

    case DETECTED_AS_SERIES: {
        REBSER *s = m_cast(REBSER*, cast(const REBSER*, p));
        assert(GET_SER_FLAG(s, SERIES_FLAG_UTF8_STRING));
        return s; }

    case DETECTED_AS_CELL: {
        const Value* v = cast(const Value*, p);
        if (ANY_WORD(v))
            return Cell_Word_Symbol(v);

        assert(ANY_STRING(v));

        // The string may be mutable, so we wouldn't want to store it
        // persistently as-is.  Consider:
        //
        //     file: copy %test
        //     x: transcode/file data1 file
        //     append file "-2"
        //     y: transcode/file data2 file
        //
        // You would not want the change of `file` to affect the filename
        // references in x's loaded source.  So the series shouldn't be used
        // directly, and as long as another reference is needed, use an
        // interned one (the same mechanic words use).
        //
        REBSIZ offset;
        REBSIZ size;
        REBSER *temp = Temp_UTF8_At_Managed(&offset, &size, v, VAL_LEN_AT(v));
        return Intern_UTF8_Managed(BIN_AT(temp, offset), size); }

    default:
        panic ("Bad pointer type passed to Intern()");
    }
}
