//
//  File: %sys-typeset.h
//  Summary: {Definitions for Typeset Values}
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
// A typeset is a collection of REB_XXX types, implemented as a 64-bit bitset.
// (Though user-defined types would clearly require a different approach to
// typechecking, using a bitset for built-in types could still be used as an
// optimization for common parameter cases.)
//
// While available to the user to manipulate directly as a TYPESET!, cells
// of this category have another use in describing the fields of objects
// ("KEYS") or parameters of function frames ("PARAMS").  When used for that
// purpose, they not only list the legal types...but also hold a symbol for
// naming the field or parameter.  R3-Alpha made these a special kind of WORD!
// called an "unword", but they lack bindings and have more technically
// in common with the evolving requirements of typesets.
//
// If values beyond REB_MAX (but still < 64) are used in the bitset, they are
// "pseudotypes", which signal properties of the typeset when acting in a
// paramlist or keylist.  REB_0 is also a pseduotype, as when the first bit
// (for 0) is set in the typeset, that means it is "<end>-able".
//
// !!! At present, a TYPESET! created with MAKE TYPESET! cannot set the
// internal symbol.  Nor can it set the pseudotype flags, though that might
// someday be allowed with a syntax like:
//
//      make typeset! [<hide> <quote> <protect> text! integer!]
//


#define IS_KIND_SYM(s) \
    ((s) < cast(REBSYM, REB_MAX))

inline static enum Reb_Kind KIND_FROM_SYM(REBSYM s) {
    assert(IS_KIND_SYM(s));
    return cast(enum Reb_Kind, cast(int, (s)));
}

#define SYM_FROM_KIND(k) \
    cast(REBSYM, cast(enum Reb_Kind, (k)))

#define VAL_TYPE_SYM(v) \
    SYM_FROM_KIND((v)->payload.datatype.kind)

inline static REBSTR *Get_Type_Name(const RELVAL *value)
    { return Canon(SYM_FROM_KIND(VAL_TYPE(value))); }



//=//// TYPESET BITS //////////////////////////////////////////////////////=//
//
// Operations when typeset is done with a bitset (currently all typesets)

#define VAL_TYPESET_BITS(v) ((v)->payload.typeset.bits)

#define TYPE_CHECK(v,n) \
    (did (VAL_TYPESET_BITS(v) & FLAGIT_KIND(n)))

#define TYPE_SET(v,n) \
    (VAL_TYPESET_BITS(v) |= FLAGIT_KIND(n))

#define TYPE_CLEAR(v,n) \
    (VAL_TYPESET_BITS(v) &= ~FLAGIT_KIND(n))

#define EQUAL_TYPESET(v,w) \
    (VAL_TYPESET_BITS(v) == VAL_TYPESET_BITS(w))

// !!! R3-Alpha made frequent use of these predefined typesets.  In Ren-C
// they have been called into question, as to exactly how copying mechanics
// should work.
 
#define TS_NOT_COPIED \
    (FLAGIT_KIND(REB_IMAGE) \
    | FLAGIT_KIND(REB_VECTOR) \
    | FLAGIT_KIND(REB_PORT))

#define TS_STD_SERIES \
    (TS_SERIES & ~TS_NOT_COPIED)

#define TS_SERIES_OBJ \
    ((TS_SERIES | TS_CONTEXT | TS_PATH) & ~TS_NOT_COPIED)

#define TS_ARRAYS_OBJ \
    ((TS_ARRAY | TS_CONTEXT | TS_PATH) & ~TS_NOT_COPIED)

#define TS_CLONE \
    (TS_SERIES & ~TS_NOT_COPIED) // currently same as TS_NOT_COPIED


//=//// PARAMETER CLASS ///////////////////////////////////////////////////=//
//
// R3-Alpha called parameter cells that were used to make keys "unwords", and
// their VAL_TYPE() dictated their parameter behavior.  Ren-C saw them more
// as being like TYPESET!s with an optional symbol, which made the code easier
// to understand and less likely to crash, which would happen when the special
// "unwords" fell into any context that would falsely interpret their bindings
// as bitsets.
//
// Yet there needed to be a place to put the parameter's class.  So it is
// packed in with the TYPESET_FLAG_XXX bits.
//
// Note: It was checked to see if giving the VAL_PARAM_CLASS() the entire byte
// and not need to mask out the flags would make a difference, but performance
// wasn't affected much.
//

enum Reb_Param_Class {
    //
    // `PARAM_CLASS_LOCAL` is a "pure" local, which will be set to null by
    // argument fulfillment.  It is indicated by a SET-WORD! in the function
    // spec, or by coming after a <local> tag in the function generators.
    //
    PARAM_CLASS_LOCAL = 0,

    // `PARAM_CLASS_NORMAL` is cued by an ordinary WORD! in the function spec
    // to indicate that you would like that argument to be evaluated normally.
    //
    //     >> foo: function [a] [print [{a is} a]]
    //
    //     >> foo 1 + 2
    //     a is 3
    //
    // Special outlier EVAL/ONLY can be used to subvert this:
    //
    //     >> eval/only :foo 1 + 2
    //     a is 1
    //     ** Script error: + does not allow void! for its value1 argument
    //
    PARAM_CLASS_NORMAL = 0x01,

    // `PARAM_CLASS_HARD_QUOTE` is cued by a GET-WORD! in the function spec
    // dialect.  It indicates that a single value of content at the callsite
    // should be passed through *literally*, without any evaluation:
    //
    //     >> foo: function [:a] [print [{a is} a]]
    //
    //     >> foo 1 + 2
    //     a is 1
    //
    //     >> foo (1 + 2)
    //     a is (1 + 2)
    //
    PARAM_CLASS_HARD_QUOTE = 0x02, // GET-WORD! in spec

    // `PARAM_CLASS_REFINEMENT`
    //
    PARAM_CLASS_REFINEMENT = 0x03,

    // `PARAM_CLASS_TIGHT` makes enfixed first arguments "lazy" and other
    // arguments will use the DO_FLAG_NO_LOOKAHEAD.
    //
    // R3-Alpha's notion of infix OP!s changed the way parameters were
    // gathered.  On the right hand side, the argument was evaluated in a
    // special mode in which further infix processing was not done.  This
    // meant that `1 + 2 * 3`, when fulfilling the 2 for the right side of +,
    // would "blind" itself so that it would not chain forward and see the
    // `* 3`.  This gave rise to a distinct behavior from `1 + multiply 2 3`.
    // A similar kind of "tightness" would happen with the left hand side,
    // where `add 1 2 * 3` would be aggressive and evaluate it as
    // `add 1 (2 * 3)` and not `(add 1 2) * 3`.
    //
    // Ren-C decouples this property so that it may be applied to any
    // parameter, and calls it "tight".  By default, however, expressions are
    // completed as far as they can be on both the left and right hand side of
    // enfixed expressions.
    //
    PARAM_CLASS_TIGHT = 0x04,

    // PARAM_CLASS_RETURN acts like a pure local, but is pre-filled with a
    // ACTION! bound to the frame, that takes 0 or 1 arg and returns it.
    //
    PARAM_CLASS_RETURN = 0x05,

    // `PARAM_CLASS_SOFT_QUOTE` is cued by a LIT-WORD! in the function spec
    // dialect.  It quotes with the exception of GROUP!, GET-WORD!, and
    // GET-PATH!...which will be evaluated:
    //
    //     >> foo: function ['a] [print [{a is} a]
    //
    //     >> foo 1 + 2
    //     a is 1
    //
    //     >> foo (1 + 2)
    //     a is 3
    //
    // Although possible to implement soft quoting with hard quoting, it is
    // a convenient way to allow callers to "escape" a quoted context when
    // they need to.
    //
    // Note: Value chosen for PCLASS_ANY_QUOTE_MASK in common with hard quote
    //
    PARAM_CLASS_SOFT_QUOTE = 0x06,

    PARAM_CLASS_UNUSED_0x07 = 0x07,

    PARAM_CLASS_MAX
};

#define PCLASS_ANY_QUOTE_MASK 0x02

#define PCLASS_NUM_BITS 3
#define PCLASS_BYTE_MASK 0x07 // for 3 bits, 0x00000111

inline static enum Reb_Param_Class VAL_PARAM_CLASS(const RELVAL *v) {
    assert(IS_TYPESET(v));
    return cast(
        enum Reb_Param_Class,
        CUSTOM_BYTE(v)
        /* (const_CUSTOM_BYTE(v) & PCLASS_BYTE_MASK) */ // resurrect if needed
    );
}

inline static void INIT_VAL_PARAM_CLASS(RELVAL *v, enum Reb_Param_Class c) {
    /* mutable_CUSTOM_BYTE(v) &= ~PCLASS_BYTE_MASK;
    mutable_CUSTOM_BYTE(v) |= c; */ // can resurrect if needed
    mutable_CUSTOM_BYTE(v) = c;
}


//=////////////////////////////////////////////////////////////////////////=//
//
// TYPESET FLAGS and PSEUDOTYPES USED AS FLAGS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Typesets could use flags encoded as TYPESET_FLAG_XXX in the type-specific
// flags byte of the header.  However, that gets somewhat cramped because
// three of those bits are used for the PARAM_CLASS.
//
// Hence an alternative option is to use out-of-range of 1...REB_MAX datatypes
// as "psuedo-types" in the typeset bits.
//
// !!! An experiment switched to using entirely pseudo-type bits, so there was
// no sharing of the PARAM_CLASS byte, to see if that sped up VAL_PARAM_CLASS
// to make a difference.  It was a somewhat minor speedup, so it has been
// kept...but could be abandoned if having more bits were at issue.
//

// Endability is distinct from optional, and it means that a parameter is
// willing to accept being at the end of the input.  This means either
// an infix dispatch's left argument is missing (e.g. `do [+ 5]`) or an
// ordinary argument hit the end (e.g. the trick used for `>> help` when
// the arity is 1 usually as `>> help foo`)
//
#define Is_Param_Endable(v) \
    TYPE_CHECK((v), REB_TS_ENDABLE)

// Indicates that when this parameter is fulfilled, it will do so with a
// value of type VARARGS!, that actually just holds a pointer to the frame
// state and allows more arguments to be gathered at the callsite *while the
// function body is running*.
//
// Note the important distinction, that a variadic parameter and taking
// a VARARGS! type are different things.  (A function may accept a
// variadic number of VARARGS! values, for instance.)
//
#define Is_Param_Variadic(v) \
    TYPE_CHECK((v), REB_TS_VARIADIC)

// Skippability is used on quoted arguments to indicate that they are willing
// to "pass" on something that isn't a matching type.  This gives an ability
// that a variadic doesn't have, which is to make decisions about rejecting
// a parameter *before* the function body runs.
//
#define Is_Param_Skippable(v) \
    TYPE_CHECK((v), REB_TS_SKIPPABLE)

// Can't be reflected (set with PROTECT/HIDE) or local in spec as `foo:`
//
#define Is_Param_Hidden(v) \
    TYPE_CHECK((v), REB_TS_HIDDEN)

// Can't be bound to beyond the current bindings.
//
// !!! This flag was implied in R3-Alpha by TYPESET_FLAG_HIDDEN.  However,
// the movement of SELF out of being a hardcoded keyword in the binding
// machinery made it start to be considered as being a by-product of the
// generator, and hence a "userspace" word (like definitional return).
// To avoid disrupting all object instances with a visible SELF, it was
// made hidden...which worked until a bugfix restored the functionality
// of checking to not bind to hidden things.  UNBINDABLE is an interim
// solution to separate the property of bindability from visibility, as
// the SELF solution shakes out--so that SELF may be hidden but bind.
//
#define Is_Param_Unbindable(v) \
    TYPE_CHECK((v), REB_TS_UNBINDABLE)

// Parameters can be marked such that if they are blank, the action will not
// be run at all.  This is done via the `<blank>` annotation, which indicates
// "handle blanks specially" (in contrast to BLANK!, which just means a
// parameter can be passed in as a blank, and the function runs normally)
//
#define Is_Param_Noop_If_Blank(v) \
    TYPE_CHECK((v), REB_TS_NOOP_IF_BLANK


#ifdef NDEBUG
    #define TYPESET_FLAG(n) \
        FLAG_LEFT_BIT(TYPE_SPECIFIC_BIT + (n))
#else
    #define TYPESET_FLAG(n) \
        (FLAG_LEFT_BIT(TYPE_SPECIFIC_BIT + (n)) | FLAG_KIND_BYTE(REB_TYPESET))
#endif


// ^-- STOP AT TYPESET_FLAG(-1) (e.g. don't use them) --^
//
// !!! TYPESET_FLAG_XXX is not currently in use, only "pseudotype" flags are.
// This is so a whole byte is taken for the parameter class, to make fetching
// and setting it faster.
//
#ifdef CPLUSPLUS_11
static_assert(0 < 8 - PCLASS_NUM_BITS, "TYPESET_FLAG_XXX too high");
#endif


//=//// PARAMETER SYMBOL //////////////////////////////////////////////////=//
//
// Name should be NULL unless typeset in object keylist or func paramlist

inline static void INIT_TYPESET_NAME(RELVAL *typeset, REBSTR *str) {
    assert(IS_TYPESET(typeset));
    typeset->extra.key_spelling = str;
}

inline static REBSTR *VAL_KEY_SPELLING(const REBCEL *v) {
    assert(CELL_KIND(v) == REB_TYPESET);
    return v->extra.key_spelling;
}

inline static REBSTR *VAL_KEY_CANON(const REBCEL *v) {
    assert(CELL_KIND(v) == REB_TYPESET);
    return STR_CANON(VAL_KEY_SPELLING(v));
}

inline static OPT_REBSYM VAL_KEY_SYM(const REBCEL *v) {
    assert(CELL_KIND(v) == REB_TYPESET);
    return STR_SYMBOL(VAL_KEY_SPELLING(v)); // mirrors canon's symbol
}

#define VAL_PARAM_SPELLING(p) VAL_KEY_SPELLING(p)
#define VAL_PARAM_CANON(p) VAL_KEY_CANON(p)
#define VAL_PARAM_SYM(p) VAL_KEY_SYM(p)


// !!! Temporary workaround--there were natives that depend on type checking
// LIT-WORD! and LIT-PATH! or would crash.  We could change those to use
// QUOTED! and force them to manually check in the native dispatcher, but
// instead keep it going with the hopes that in the future typesets will
// become more sophisticated and be able to expand beyond their 64-bit limit
// to account for generic quoting.
//
inline static bool Typecheck_Including_Quoteds(const RELVAL *param, const RELVAL *v) {
    if (TYPE_CHECK(param, VAL_TYPE(v)))
        return true;

    if (KIND_BYTE(v) == REB_WORD + REB_64)  // what was a "lit word"
        if (TYPE_CHECK(param, REB_TS_QUOTED_WORD))
            return true;

    if (KIND_BYTE(v) == REB_PATH + REB_64) // what was a "lit path"
        if (TYPE_CHECK(param, REB_TS_QUOTED_PATH))
            return true;

    return false;
}
