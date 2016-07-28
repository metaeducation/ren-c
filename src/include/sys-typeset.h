//
//  File: %sys-typeset.h
//  Summary: {Definitions for Typeset Values}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2016 Rebol Open Source Contributors
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
// A typeset is a collection of up to 63 types, implemented as a bitset.
// The 0th type corresponds to REB_TRASH and can be used to indicate another
// property of the typeset (though no such uses exist yet).
//
// !!! The limit of only being able to hold a set of 63 types is a temporary
// one, as user-defined types will require a different approach.  Hence the
// best way to look at the bitset for built-in types is as an optimization
// for type-checking the common parameter cases.
//
// Though available to the user to manipulate directly as a TYPESET!, REBVALs
// of this category have another use in describing the fields of objects
// ("KEYS") or parameters of function frames ("PARAMS").  When used for that
// purpose, they not only list the legal types...but also hold a symbol for
// naming the field or parameter.
//
// !!! At present, a TYPESET! created with MAKE TYPESET! cannot set the
// internal symbol.  Nor can it set the extended flags, though that might
// someday be allowed with a syntax like:
//
//      make typeset! [<hide> <quote> <protect> string! integer!]
//

enum Reb_Param_Class {
    PARAM_CLASS_0 = 0, // reserve to catch uninitialized cases

    // `PARAM_CLASS_NORMAL` is cued by an ordinary WORD! in the function spec
    // to indicate that you would like that argument to be evaluated normally.
    //
    //     >> foo: function [a] [print [{a is} a]
    //
    //     >> foo 1 + 2
    //     a is 3
    //
    // Special outlier EVAL/ONLY can be used to subvert this:
    //
    //     >> eval/only :foo 1 + 2
    //     a is 1
    //     ** Script error: + operator is missing an argument
    //
    PARAM_CLASS_NORMAL = 0x01,

    // `PARAM_CLASS_HARD_QUOTE` is cued by a GET-WORD! in the function spec
    // dialect.  It indicates that a single value of  content at the callsite
    // should be passed through *literally*, without any evaluation:
    //
    //     >> foo: function [:a] [print [{a is} a]
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

    // `PARAM_CLASS_LOCAL` is a "pure" local, which will be set to void by
    // argument fulfillment.  It is indicated by a SET-WORD! in the function
    // spec, or by coming after a <local> tag in the function generators.
    //
    // !!! Initially these were indicated with TYPESET_FLAG_HIDDEN.  That
    // would allow the PARAM_CLASS to fit in just two bits (if there were
    // no debug-purpose PARAM_CLASS_0) and free up a scarce typeset flag.
    // But is it the case that hiding and localness should be independent?
    //
    PARAM_CLASS_LOCAL = 0x04,

    // PARAM_CLASS_RETURN acts like a pure local, but is pre-filled with a
    // definitionally-scoped function value that takes 1 arg and returns it.
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

    // `PARAM_CLASS_LEAVE` acts like a pure local, but is pre-filled with a
    // definitionally-scoped function value that takes 0 args and returns void
    //
    PARAM_CLASS_LEAVE = 0x07,

    PARAM_CLASS_MAX
};

#define PCLASS_ANY_QUOTE_MASK 0x02

#define PCLASS_MASK \
    (cast(REBUPT, 0x07) << TYPE_SPECIFIC_BIT)


#ifdef NDEBUG
    #define TYPESET_FLAG(n) \
        (cast(REBUPT, 1) << (TYPE_SPECIFIC_BIT + (n)))
#else
    #define TYPESET_FLAG(n) \
        ((cast(REBUPT, 1) << (TYPE_SPECIFIC_BIT + (n))) \
            | TYPE_SHIFT_LEFT_FOR_HEADER(REB_TYPESET))
#endif

// Option flags used with GET_VAL_FLAG().  These describe properties of
// a value slot when it's constrained to the types in the typeset
//

// Can't be changed (set with PROTECT)
//
#define TYPESET_FLAG_LOCKED TYPESET_FLAG(3)

// Can't be reflected (set with PROTECT/HIDE) or local in spec as `foo:`
//
#define TYPESET_FLAG_HIDDEN TYPESET_FLAG(4)

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
#define TYPESET_FLAG_UNBINDABLE TYPESET_FLAG(5)

// !!! <durable> is the working name for the property of a function
// argument or local to have its data survive after the call is over.
// Much of the groundwork has been laid to allow this to be specified
// individually for each argument, but the feature currently is "all
// or nothing"--and implementation-wise corresponds to what R3-Alpha
// called CLOSURE!, with the deep-copy-per-call that entails.
//
// Hence if this property is applied, it will be applied to *all* of
// a function's arguments.
//
#define TYPESET_FLAG_DURABLE TYPESET_FLAG(6)

// !!! This does not need to be on the typeset necessarily.  See the
// VARARGS! type for what this is, which is a representation of the
// capture of an evaluation position. The type will also be checked but
// the value will not be consumed.
//
// Note the important distinction, that a variadic parameter and taking
// a VARARGS! type are different things.  (A function may accept a
// variadic number of VARARGS! values, for instance.)
//
#define TYPESET_FLAG_VARIADIC TYPESET_FLAG(7)

// !!! In R3-Alpha, there were only 8 type-specific bits...with the
// remaining bits "reserved for future use".  This goes over the line
// with a 9th type-specific bit, which may or may not need review.
// It could just be that more type-specific bits is the future use.

// Endability is distinct from optional, and it means that a parameter is
// willing to accept being at the end of the input.  This means either
// an infix dispatch's left argument is missing (e.g. `do [+ 5]`) or an
// ordinary argument hit the end (e.g. the trick used for `>> help` when
// the arity is 1 usually as `>> help foo`)
//
#define TYPESET_FLAG_ENDABLE TYPESET_FLAG(8)

// For performance, a cached PROTECTED_OR_LOOKBACK or'd flag could make
// it so that each SET doesn't have to clear out the flag.  See
// notes on that in variable setting.  The negative sense is chosen
// so that the TRUE value can mean REB_FUNCTION (chosen at type #1) and
// the FALSE value occupies non-value-type REB_0, alias REB_0_LOOKBACK
//
#define TYPESET_FLAG_NO_LOOKBACK TYPESET_FLAG(9)


// Operations when typeset is done with a bitset (currently all typesets)

#define VAL_TYPESET_BITS(v) ((v)->payload.typeset.bits)

#define TYPE_CHECK(v,n) \
    LOGICAL(VAL_TYPESET_BITS(v) & FLAGIT_KIND(n))

#define TYPE_SET(v,n) \
    ((VAL_TYPESET_BITS(v) |= FLAGIT_KIND(n)), NOOP)

#define EQUAL_TYPESET(v,w) \
    (VAL_TYPESET_BITS(v) == VAL_TYPESET_BITS(w))


// Name should be NULL unless typeset in object keylist or func paramlist

inline static void INIT_TYPESET_NAME(RELVAL *typeset, REBSTR *str) {
    assert(IS_TYPESET(typeset));
    typeset->extra.key_spelling = str;
}

inline static REBSTR *VAL_KEY_SPELLING(const RELVAL *typeset) {
    assert(IS_TYPESET(typeset));
    return typeset->extra.key_spelling;
}

inline static REBSTR *VAL_KEY_CANON(const RELVAL *typeset) {
    return STR_CANON(VAL_KEY_SPELLING(typeset));
}

inline static OPT_REBSYM VAL_KEY_SYM(const RELVAL *typeset) {
    return STR_SYMBOL(VAL_KEY_SPELLING(typeset)); // mirrors canon's symbol
}

#define VAL_PARAM_SPELLING(p) VAL_KEY_SPELLING(p)
#define VAL_PARAM_CANON(p) VAL_KEY_CANON(p)
#define VAL_PARAM_SYM(p) VAL_KEY_SYM(p)

inline static enum Reb_Param_Class VAL_PARAM_CLASS(const RELVAL *v) {
    assert(IS_TYPESET(v));
    return cast(
        enum Reb_Param_Class,
        (v->header.bits & PCLASS_MASK) >> TYPE_SPECIFIC_BIT
    );
}

inline static void INIT_VAL_PARAM_CLASS(RELVAL *v, enum Reb_Param_Class c) {
    v->header.bits &= ~PCLASS_MASK;
    v->header.bits |= cast(REBUPT, c << TYPE_SPECIFIC_BIT);
}
