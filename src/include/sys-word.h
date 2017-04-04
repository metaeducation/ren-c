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
        FLAGIT_LEFT(TYPE_SPECIFIC_BIT + (n))
#else
    #define WORD_FLAG(n) \
        (FLAGIT_LEFT(TYPE_SPECIFIC_BIT + (n)) | HEADERIZE_KIND(REB_WORD))
#endif

// `WORD_FLAG_BOUND` answers whether a word is bound, but it may be
// relatively bound if `VALUE_FLAG_RELATIVE` is set.  In that case, it
// does not have a context pointer but rather a function pointer, that
// must be combined with more information to get the FRAME! where the
// word should actually be looked up.
//
// If VALUE_FLAG_RELATIVE is set, then WORD_FLAG_BOUND must also be set.
//
#define WORD_FLAG_BOUND WORD_FLAG(0)


#define IS_WORD_BOUND(v) \
    Get_Val_Flag((v), WORD_FLAG_BOUND)

#define IS_WORD_UNBOUND(v) \
    NOT(IS_WORD_BOUND(v))

inline static REBSTR *VAL_WORD_SPELLING(const RELVAL *v) {
    assert(ANY_WORD(v));
    return v->payload.any_word.spelling;
}

inline static REBSTR *VAL_WORD_CANON(const RELVAL *v) {
    assert(ANY_WORD(v));
    return STR_CANON(v->payload.any_word.spelling);
}

inline static OPT_REBSYM VAL_WORD_SYM(const RELVAL *v) {
    return STR_SYMBOL(v->payload.any_word.spelling);
}

inline static const REBYTE *VAL_WORD_HEAD(const RELVAL *v) {
    return STR_HEAD(VAL_WORD_SPELLING(v)); // '\0' terminated UTF-8
}

inline static void INIT_WORD_CONTEXT(RELVAL *v, REBCTX *context) {
    assert(Get_Val_Flag(v, WORD_FLAG_BOUND) && context != SPECIFIED);
    ENSURE_SERIES_MANAGED(CTX_SERIES(context));
    ASSERT_ARRAY_MANAGED(CTX_KEYLIST(context));
    v->extra.binding = CTX_VARLIST(context);
}

inline static REBCTX *VAL_WORD_CONTEXT(const REBVAL *v) {
    assert(Get_Val_Flag((v), WORD_FLAG_BOUND));
    return VAL_SPECIFIC(v);
}

inline static void INIT_WORD_FUNC(RELVAL *v, REBFUN *func) {
    assert(Get_Val_Flag(v, WORD_FLAG_BOUND));
    v->extra.binding = FUNC_PARAMLIST(func);
}

inline static REBFUN *VAL_WORD_FUNC(const RELVAL *v) {
    assert(Get_Val_Flag(v, WORD_FLAG_BOUND));
    return VAL_RELATIVE(v);
}

inline static void INIT_WORD_INDEX(RELVAL *v, REBCNT i) {
    assert(ANY_WORD(v));
    assert(Get_Val_Flag((v), WORD_FLAG_BOUND));
    assert(SAME_STR(
        VAL_WORD_SPELLING(v),
        IS_RELATIVE(v)
            ? VAL_KEY_SPELLING(FUNC_PARAM(VAL_WORD_FUNC(v), i))
            : CTX_KEY_SPELLING(VAL_WORD_CONTEXT(KNOWN(v)), i)
    ));
    v->payload.any_word.index = cast(REBINT, i);
}

inline static REBCNT VAL_WORD_INDEX(const RELVAL *v) {
    assert(ANY_WORD(v));
    REBINT i = v->payload.any_word.index;
    assert(i > 0);
    return cast(REBCNT, i);
}

inline static void Unbind_Any_Word(RELVAL *v) {
    Clear_Val_Flags(v, WORD_FLAG_BOUND | VALUE_FLAG_RELATIVE);
#if !defined(NDEBUG)
    v->payload.any_word.index = 0;
#endif
}

inline static void Init_Any_Word(
    RELVAL *out,
    enum Reb_Kind kind,
    REBSTR *spelling
) {
    Reset_Val_Header(out, kind);

    assert(spelling != NULL);
    out->payload.any_word.spelling = spelling;

#if !defined(NDEBUG)
    out->payload.any_word.index = 0;
#endif

    assert(ANY_WORD(out));
    assert(IS_WORD_UNBOUND(out));
}

#define Init_Word(out,spelling) \
    Init_Any_Word((out), REB_WORD, (spelling))

#define Init_Get_Word(out,spelling) \
    Init_Any_Word((out), REB_GET_WORD, (spelling))

#define Init_Set_Word(out,spelling) \
    Init_Any_Word((out), REB_SET_WORD, (spelling))

#define Init_Lit_Word(out,spelling) \
    Init_Any_Word((out), REB_LIT_WORD, (spelling))

#define Init_Refinement(out,spelling) \
    Init_Any_Word((out), REB_REFINEMENT, (spelling))

#define Init_Issue(out,spelling) \
    Init_Any_Word((out), REB_ISSUE, (spelling))

// Initialize an ANY-WORD! type with a binding to a context.
//
inline static void Init_Any_Word_Bound(
    REBVAL *out,
    enum Reb_Kind type,
    REBSTR *spelling,
    REBCTX *context,
    REBCNT index
) {
    assert(CTX_KEY_CANON(context, index) == STR_CANON(spelling));

    Reset_Val_Header_Core(out, type, WORD_FLAG_BOUND);

    assert(spelling != NULL);
    out->payload.any_word.spelling = spelling;

    INIT_WORD_CONTEXT(out, context);
    INIT_WORD_INDEX(out, index);

    assert(ANY_WORD(out));
    assert(IS_WORD_BOUND(out));
}

inline static void Canonize_Any_Word(REBVAL *any_word) {
    any_word->payload.any_word.spelling = VAL_WORD_CANON(any_word);
}
