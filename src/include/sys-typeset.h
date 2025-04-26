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
// A typeset is a collection of TYPE_XXX types, implemented as a 64-bit bitset.
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
// If values beyond TYPE_MAX (but still < 64) are used in the bitset, they are
// "pseudotypes", which signal properties of the typeset when acting in a
// paramlist or keylist.  TYPE_0 is also a pseduotype, as when the first bit
// (for 0) is set in the typeset, that means it is "<end>-able".
//
// !!! At present, a TYPESET! created with MAKE TYPESET! cannot set the
// internal symbol.  Nor can it set the pseudotype flags, though that might
// someday be allowed with a syntax like:
//
//      make typeset! [<hide> <quote> <protect> text! integer!]
//


#define IS_KIND_SYM(s) \
    ((s) < cast(SymId, TYPE_MAX))

INLINE enum Reb_Kind KIND_FROM_SYM(SymId s) {
    assert(IS_KIND_SYM(s));
    return cast(enum Reb_Kind, cast(int, (s)));
}

#define SYM_FROM_KIND(k) \
    cast(SymId, cast(enum Reb_Kind, (k)))

#define VAL_TYPE_SYM(v) \
    SYM_FROM_KIND((v)->payload.datatype.kind)

INLINE Symbol* Get_Type_Name(const Cell* value)
    { return CANON(FROM_KIND(Type_Of(value))); }



//=//// TYPESET BITS //////////////////////////////////////////////////////=//
//
// Operations when typeset is done with a bitset (currently all typesets)

#define Cell_Typeset_Bits(v) ((v)->payload.typeset.bits)

#define Typeset_Check(v,n) \
    (did (Cell_Typeset_Bits(v) & FLAGIT_KIND(n)))

#define Set_Typeset_Flag(v,n) \
    (Cell_Typeset_Bits(v) |= FLAGIT_KIND(n))

#define Clear_Typeset_Flag(v,n) \
    (Cell_Typeset_Bits(v) &= ~FLAGIT_KIND(n))

#define Typesets_Equal(v,w) \
    (Cell_Typeset_Bits(v) == Cell_Typeset_Bits(w))

// !!! R3-Alpha made frequent use of these predefined typesets.  In Ren-C
// they have been called into question, as to exactly how copying mechanics
// should work.

#define TS_NOT_COPIED FLAGIT_KIND(TYPE_PORT)

#define TS_STD_SERIES \
    (TS_SERIES & ~TS_NOT_COPIED)

#define TS_SERIES_OBJ \
    ((TS_SERIES | TS_CONTEXT) & ~TS_NOT_COPIED)

#define TS_LISTS_OBJ \
    ((TS_LIST | TS_CONTEXT) & ~TS_NOT_COPIED)

#define TS_CLONE \
    (TS_SERIES & ~TS_NOT_COPIED) // currently same as TS_NOT_COPIED


//=//// PARAMETER CLASS ///////////////////////////////////////////////////=//
//
// R3-Alpha called parameter cells that were used to make keys "unwords", and
// their Type_Of() dictated their parameter behavior.  Ren-C saw them more
// as being like TYPESET!s with an optional symbol, which made the code easier
// to understand and less likely to crash, which would happen when the special
// "unwords" fell into any context that would falsely interpret their bindings
// as bitsets.
//
// Yet there needed to be a place to put the parameter's class.  So it is
// packed in to the typeset header (via the CUSTOM_BYTE())
//
// Note: It was checked to see if giving the Cell_Parameter_Class() the entire byte
// and not need to mask out the flags would make a difference, but performance
// wasn't affected much.
//

typedef enum {
    //
    // `PARAMCLASS_LOCAL` is a "pure" local, which will be set to null by
    // argument fulfillment.  It is indicated by a SET-WORD! in the function
    // spec, or by coming after a <local> tag in the function generators.
    //
    PARAMCLASS_LOCAL = 0,

    // `PARAMCLASS_NORMAL` is cued by an ordinary WORD! in the function spec
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
    //     ** Script error: + does not allow trash for its value1 argument
    //
    PARAMCLASS_NORMAL = 0x01,

    // `PARAMCLASS_HARD_QUOTE` is cued by a GET-WORD! in the function spec
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
    PARAMCLASS_HARD_QUOTE = 0x02, // GET-WORD! in spec

    // `PARAMCLASS_REFINEMENT`
    //
    PARAMCLASS_REFINEMENT = 0x03,

    PARAMCLASS_UNUSED_0x04 = 0x04,

    // PARAMCLASS_RETURN acts like a pure local, but is pre-filled with a
    // ACTION! bound to the frame, that takes 0 or 1 arg and returns it.
    //
    PARAMCLASS_RETURN = 0x05,

    // `PARAMCLASS_SOFT_QUOTE` is cued by a LIT-WORD! in the function spec
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
    PARAMCLASS_SOFT_QUOTE = 0x06,

    PARAMCLASS_UNUSED_0x07 = 0x07,

    MAX_PARAMCLASS = PARAMCLASS_UNUSED_0x07
} ParamClass;

#define PCLASS_ANY_QUOTE_MASK 0x02

#define PCLASS_NUM_BITS 3
#define PCLASS_BYTE_MASK 0x07 // for 3 bits, 0x00000111

STATIC_ASSERT(MAX_PARAMCLASS <= PCLASS_BYTE_MASK);

INLINE ParamClass Cell_Parameter_Class(const Cell* v) {
    assert(Is_Typeset(v));
    return cast(
        ParamClass,
        CUSTOM_BYTE(v)
        /* (CUSTOM_BYTE(v) & PCLASS_BYTE_MASK) */ // resurrect if needed
    );
}

INLINE void Tweak_Parameter_Class(Cell* v, ParamClass c) {
    /* CUSTOM_BYTE(v) &= ~PCLASS_BYTE_MASK;
    CUSTOM_BYTE(v) |= c; */ // can resurrect if needed
    CUSTOM_BYTE(v) = c;
}


//=////////////////////////////////////////////////////////////////////////=//
//
// TYPESET FLAGS and PSEUDOTYPES USED AS FLAGS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Typesets could use flags encoded in the type-specific flags byte of the
// header.  However, that gets somewhat cramped because three of those bits
// are used for the PARAM_CLASS.
//
// Hence an alternative option is to use out-of-range of 1...TYPE_MAX datatypes
// as "psuedo-types" in the typeset bits.
//
// !!! An experiment switched to using entirely pseudo-type bits, so there was
// no sharing of the PARAM_CLASS byte, to see if that sped up Cell_Parameter_Class
// to make a difference.  It was a somewhat minor speedup, so it has been
// kept...but could be abandoned if having more bits were at issue.
//

// Endability is distinct from optional, and it means that a parameter is
// willing to accept being at the end of the input.  This means either
// an infix dispatch's left argument is missing (e.g. `eval [+ 5]`) or an
// ordinary argument hit the end (e.g. the trick used for `>> help` when
// the arity is 1 usually as `>> help foo`)
//
#define TYPE_TS_ENDABLE TYPE_0
#define Is_Param_Endable(v) \
    Typeset_Check((v), TYPE_TS_ENDABLE)

// Indicates that when this parameter is fulfilled, it will do so with a
// value of type VARARGS!, that actually just holds a pointer to the frame
// state and allows more arguments to be gathered at the callsite *while the
// function body is running*.
//
// Note the important distinction, that a variadic parameter and taking
// a VARARGS! type are different things.  (A function may accept a
// variadic number of VARARGS! values, for instance.)
//
#define TYPE_TS_VARIADIC TYPE_MAX_PLUS_ONE
#define Is_Param_Variadic(v) \
    Typeset_Check((v), TYPE_TS_VARIADIC)

// Skippability is used on quoted arguments to indicate that they are willing
// to "pass" on something that isn't a matching type.  This gives an ability
// that a variadic doesn't have, which is to make decisions about rejecting
// a parameter *before* the function body runs.
//
#define TYPE_TS_SKIPPABLE TYPE_MAX_PLUS_TWO
#define Is_Param_Skippable(v) \
    Typeset_Check((v), TYPE_TS_SKIPPABLE)

// Can't be reflected (set with PROTECT/HIDE) or local in spec as `foo:`
//
#define TYPE_TS_HIDDEN TYPE_MAX_PLUS_THREE
#define Is_Param_Hidden(v) \
    Typeset_Check((v), TYPE_TS_HIDDEN)

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
#define TYPE_TS_UNBINDABLE TYPE_MAX_PLUS_FOUR
#define Is_Param_Unbindable(v) \
    Typeset_Check((v), TYPE_TS_UNBINDABLE)

// Parameters can be marked such that if they are blank, the action will not
// be run at all.  This is done via the `<maybe>` annotation, which indicates
// "handle blanks specially" (in contrast to BLANK!, which just means a
// parameter can be passed in as a blank, and the function runs normally)
//
#define TYPE_TS_NOOP_IF_VOID \
    TYPE_MAX_PLUS_FIVE


//=//// PARAMETER SYMBOL //////////////////////////////////////////////////=//
//
// Name should be nullptr unless typeset in object keylist or func paramlist

INLINE void INIT_TYPESET_NAME(Cell* typeset, Symbol* symbol) {
    assert(Is_Typeset(typeset));
    typeset->extra.key_symbol = symbol;
}

INLINE Symbol* Key_Symbol(const Cell* typeset) {
    assert(Is_Typeset(typeset));
    return typeset->extra.key_symbol;
}

INLINE Symbol* Key_Canon(const Cell* typeset) {
    return Canon_Symbol(Key_Symbol(typeset));
}

INLINE Option(SymId) Key_Id(const Cell* typeset) {
    return Symbol_Id(Key_Symbol(typeset)); // mirrors canon's symbol
}

#define Cell_Parameter_Symbol(p) Key_Symbol(p)
#define Cell_Param_Canon(p) Key_Canon(p)
#define Cell_Parameter_Id(p) Key_Id(p)
