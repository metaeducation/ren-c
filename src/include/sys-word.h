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
// implemented as a REBSTR UTF-8 string (see %sys-string.h), and can act as
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


inline static bool IS_WORD_UNBOUND(const REBCEL *v) {
    assert(ANY_WORD_KIND(CELL_KIND(v)));
    return v->extra.binding == nullptr;
}

#define IS_WORD_BOUND(v) \
    (not IS_WORD_UNBOUND(v))

inline static REBSTR *VAL_WORD_SPELLING(const REBCEL *v) {
    assert(ANY_WORD_KIND(CELL_KIND(v)));
    return v->payload.any_word.spelling;
}

inline static REBSTR *VAL_WORD_CANON(const REBCEL *v) {
    assert(ANY_WORD_KIND(CELL_KIND(v)));
    return STR_CANON(v->payload.any_word.spelling);
}

// Some scenarios deliberately store canon spellings in words, to avoid
// needing to re-canonize them.  If you have one of those words, use this to
// add a check that your assumption about them is correct.
//
// Note that canon spellings can get GC'd, effectively changing the canon.
// But they won't if there are any words outstanding that hold that spelling,
// so this is a safe technique as long as these words are GC-mark-visible.
//
inline static REBSTR *VAL_STORED_CANON(const REBCEL *v) {
    assert(ANY_WORD_KIND(CELL_KIND(v)));
    assert(GET_SER_INFO(v->payload.any_word.spelling, STRING_INFO_CANON));
    return v->payload.any_word.spelling;
}

inline static OPT_REBSYM VAL_WORD_SYM(const REBCEL *v) {
    assert(ANY_WORD_KIND(CELL_KIND(v)));
    return STR_SYMBOL(v->payload.any_word.spelling);
}

inline static REBCTX *VAL_WORD_CONTEXT(const REBVAL *v) {
    assert(IS_WORD_BOUND(v));
    REBNOD *binding = VAL_BINDING(v);
    assert(
        GET_SER_FLAG(binding, NODE_FLAG_MANAGED)
        or IS_END(FRM(LINK(binding).keysource)->param) // not fulfilling
    );
    binding->header.bits |= NODE_FLAG_MANAGED; // !!! review managing needs
    return CTX(binding);
}

inline static void INIT_WORD_INDEX(RELVAL *v, REBCNT i) {
  #if !defined(NDEBUG)
    INIT_WORD_INDEX_Extra_Checks_Debug(v, i); // not inline, needs FRM_PHASE()
  #endif
    v->payload.any_word.index = cast(REBINT, i);
}

inline static REBCNT VAL_WORD_INDEX(const REBCEL *v) {
    assert(IS_WORD_BOUND(v));
    REBINT i = v->payload.any_word.index;
    assert(i > 0);
    return cast(REBCNT, i);
}

inline static void Unbind_Any_Word(RELVAL *v) {
    INIT_BINDING(v, UNBOUND);
#if !defined(NDEBUG)
    v->payload.any_word.index = 0;
#endif
}

inline static REBVAL *Init_Any_Word(
    RELVAL *out,
    enum Reb_Kind kind,
    REBSTR *spelling
){
    RESET_CELL(out, kind);
    out->payload.any_word.spelling = spelling;
    INIT_BINDING(out, UNBOUND);
  #if !defined(NDEBUG)
    out->payload.any_word.index = 0; // index not heeded if no binding
  #endif
    return KNOWN(out);
}

#define Init_Word(out,spelling) \
    Init_Any_Word((out), REB_WORD, (spelling))

#define Init_Get_Word(out,spelling) \
    Init_Any_Word((out), REB_GET_WORD, (spelling))

#define Init_Set_Word(out,spelling) \
    Init_Any_Word((out), REB_SET_WORD, (spelling))

#define Init_Issue(out,spelling) \
    Init_Any_Word((out), REB_ISSUE, (spelling))

// Initialize an ANY-WORD! type with a binding to a context.
//
inline static REBVAL *Init_Any_Word_Bound(
    RELVAL *out,
    enum Reb_Kind type,
    REBSTR *spelling,
    REBCTX *context,
    REBCNT index
) {
    RESET_CELL(out, type);
    out->payload.any_word.spelling = spelling;
    INIT_BINDING(out, context);
    INIT_WORD_INDEX(out, index);
    return KNOWN(out);
}


// To make interfaces easier for some functions that take REBSTR* strings,
// it can be useful to allow passing UTF-8 text, a REBVAL* with an ANY-WORD!
// or ANY-STRING!, or just plain UTF-8 text.
//
// !!! Should NULLED_CELL or other arguments make anonymous symbols?
//
#ifdef CPLUSPLUS_11
template<typename T>
inline static REBSTR* Intern(const T *p)
{
    static_assert(
        std::is_same<T, REBVAL>::value
        or std::is_same<T, char>::value
        or std::is_same<T, REBSTR>::value,
        "STR works on: char*, REBVAL*, REBSTR*"
    );
#else
inline static REBSTR* Intern(const void *p)
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
        const REBVAL *v = cast(const REBVAL*, p);
        if (ANY_WORD(v))
            return VAL_WORD_SPELLING(v);

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
