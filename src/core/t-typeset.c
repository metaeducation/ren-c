//
//  File: %t-typeset.c
//  Summary: "typeset datatype"
//  Section: datatypes
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include "sys-core.h"


//
// symbol-to-typeset-bits mapping table
//
// NOTE: Order of symbols is important, because this is used to build a
// list of typeset word symbols ordered relative to their symbol #,
// which lays out the legal unbound WORD! values you can use during
// a MAKE TYPESET! (bound words will be looked up as variables to see
// if they contain a DATATYPE! or a typeset, but general reduction is
// not performed on the block passed in.)
//
// !!! Is it necessary for MAKE TYPESET! to allow unbound words at all,
// or should the typesets be required to be in bound variables?  Should
// clients be asked to pass in only datatypes and typesets, hence doing
// their own reduce before trying to make a typeset out of a block?
//
const struct {
    REBSYM sym;
    REBU64 bits;
} Typesets[] = {
    {SYM_ANY_VALUE_X, TS_VALUE},
    {SYM_ANY_WORD_X, TS_WORD},
    {SYM_ANY_PATH_X, TS_PATH},
    {SYM_ANY_NUMBER_X, TS_NUMBER},
    {SYM_ANY_SCALAR_X, TS_SCALAR},
    {SYM_ANY_SEQUENCE_X, TS_SEQUENCE},
    {SYM_ANY_TUPLE_X, TS_TUPLE},
    {SYM_ANY_SERIES_X, TS_SERIES},
    {SYM_ANY_STRING_X, TS_STRING},
    {SYM_ANY_CONTEXT_X, TS_CONTEXT},
    {SYM_ANY_ARRAY_X, TS_ARRAY},
    {SYM_ANY_BRANCH_X, TS_BRANCH},

    {SYM_0, 0}
};


//
//  CT_Typeset: C
//
REBINT CT_Typeset(REBCEL(const*) a, REBCEL(const*) b, bool strict)
{
    UNUSED(strict);
    if (EQUAL_TYPESET(a, b))
        return 0;
    return a > b ? 1 : -1;  // !!! Bad arbitrary comparison, review
}


//
//  Startup_Typesets: C
//
// Create typeset variables that are defined above.
// For example: NUMBER is both integer and decimal.
// Add the new variables to the system context.
//
void Startup_Typesets(void)
{
    REBDSP dsp_orig = DSP;

    REBINT n;
    for (n = 0; Typesets[n].sym != 0; n++) {
        Init_Typeset(DS_PUSH(), Typesets[n].bits);

        Move_Value(
            Append_Context(Lib_Context, NULL, Canon(Typesets[n].sym)),
            DS_TOP
        );
    }

    // !!! Why does the system access the typesets through Lib_Context, vs.
    // using the Root_Typesets?
    //
    Root_Typesets = Init_Block(Alloc_Value(), Pop_Stack_Values(dsp_orig));
    Force_Value_Frozen_Deep(Root_Typesets);
}


//
//  Shutdown_Typesets: C
//
void Shutdown_Typesets(void)
{
    rebRelease(Root_Typesets);
    Root_Typesets = NULL;
}


//
//  Add_Typeset_Bits_Core: C
//
// This sets the bits in a bitset according to a block of datatypes.  There
// is special handling by which BAR! will set the "variadic" bit on the
// typeset, which is heeded by functions only.
//
// !!! R3-Alpha supported fixed word symbols for datatypes and typesets.
// Confusingly, this means that if you have said `word!: integer!` and use
// WORD!, you will get the integer type... but if WORD! is unbound then it
// will act as WORD!.  Also, is essentially having "keywords" and should be
// reviewed to see if anything actually used it.
//
bool Add_Typeset_Bits_Core(
    RELVAL *typeset,
    const RELVAL *head,
    REBSPC *specifier
) {
    assert(IS_TYPESET(typeset) or IS_PARAM(typeset));

    const RELVAL *maybe_word = head;
    for (; NOT_END(maybe_word); ++maybe_word) {
        const RELVAL *item;
        if (IS_WORD(maybe_word))
            item = Lookup_Word_May_Fail(maybe_word, specifier);
        else
            item = maybe_word; // wasn't variable

        if (IS_TUPLE(item) and rebDidQ("equal?", item, "'<...>", rebEND)) {
            //
            // !!! The actual final notation for variadics is not decided
            // on, so there is compatibility for now with the <...> form
            // from when that was a TAG! vs. a 5-element TUPLE!  While
            // core sources were changed to `<variadic>`, asking users
            // to shuffle should only be done once (when final is known).
            //
            TYPE_SET(typeset, REB_TS_VARIADIC);
        }
        else if (IS_TAG(item)) {
            bool strict = false;

            if (
                0 == CT_String(item, Root_Variadic_Tag, strict)
            ){
                // !!! The actual final notation for variadics is not decided
                // on, so there is compatibility for now with the <...> form
                // from when that was a TAG! vs. a 5-element TUPLE!  While
                // core sources were changed to `<variadic>`, asking users
                // to shuffle should only be done once (when final is known).
                //
                TYPE_SET(typeset, REB_TS_VARIADIC);
            }
            else if (0 == CT_String(item, Root_End_Tag, strict)) {
                TYPE_SET(typeset, REB_TS_ENDABLE);
            }
            else if (0 == CT_String(item, Root_Blank_Tag, strict)) {
                TYPE_SET(typeset, REB_TS_NOOP_IF_BLANK);
            }
            else if (0 == CT_String(item, Root_Opt_Tag, strict)) {
                //
                // !!! Review if this makes sense to allow with MAKE TYPESET!
                // instead of just function specs.
                //
                TYPE_SET(typeset, REB_NULL);
            }
            else if (0 == CT_String(item, Root_Invisible_Tag, strict)) {
                TYPE_SET(typeset, REB_TS_INVISIBLE);  // !!! REB_BYTES hack
            }
            else if (0 == CT_String(item, Root_Output_Tag, strict)) {
                //
                // !!! Typeset bits are currently scarce, so output is being
                // indicated solely by being a refinement and having this
                // set of potential argument types.  It represents little
                // risk at time of writing to disrupting existing code during
                // the multiple return values prototyping stage, because it
                // only has an effect when you use a SET-BLOCK! to the left.
                //
                CLEAR_ALL_TYPESET_BITS(typeset);
                TYPE_SET(typeset, REB_TS_REFINEMENT);
                TYPE_SET(typeset, REB_NULL);
                TYPE_SET(typeset, REB_ISSUE);  // see Is_Blackhole()
                TYPE_SET(typeset, REB_WORD);
                TYPE_SET(typeset, REB_PATH);
                break;
            }
            else if (0 == CT_String(item, Root_Skip_Tag, strict)) {
                if (VAL_PARAM_CLASS(typeset) != REB_P_HARD_QUOTE)
                    fail ("Only hard-quoted parameters are <skip>-able");

                TYPE_SET(typeset, REB_TS_SKIPPABLE);
                TYPE_SET(typeset, REB_TS_ENDABLE); // skip => null
                TYPE_SET(typeset, REB_NULL);  // null if specialized
            }
            else if (0 == CT_String(item, Root_Const_Tag, strict)) {
                TYPE_SET(typeset, REB_TS_CONST);
            }
            else if (0 == CT_String(item, Root_Modal_Tag, strict)) {
                //
                // !!! <modal> is not the general way to make modal args (the
                // `@arg` notation is used), but the native specs are loaded
                // by a boostrap r3 that can't read them.
                //
                mutable_KIND3Q_BYTE(typeset) = REB_P_MODAL;
            }
        }
        else if (IS_DATATYPE(item)) {
            //
            // !!! For the moment, all REB_CUSTOM types are glommed
            // together into the same typeset test.  Doing better will
            // involve a redesign of typesets from R3-Alpha's 64 bits.
            //
            TYPE_SET(typeset, VAL_TYPE_KIND_OR_CUSTOM(item));
        }
        else if (IS_TYPESET(item)) {
            VAL_TYPESET_LOW_BITS(typeset) |= VAL_TYPESET_LOW_BITS(item);
            VAL_TYPESET_HIGH_BITS(typeset) |= VAL_TYPESET_HIGH_BITS(item);
        }
        else if (IS_SYM_WORD(item)) {  // see Startup_Fake_Type_Constraint()
            switch (VAL_WORD_SYM(item)) {
              case SYM_CHAR_X:
              case SYM_BLACKHOLE_X:
                TYPE_SET(typeset, REB_ISSUE);
                break;

              case SYM_LIT_WORD_X:
              case SYM_LIT_PATH_X:
                TYPE_SET(typeset, REB_QUOTED);
                break;

              case SYM_REFINEMENT_X:
                TYPE_SET(typeset, REB_PATH);
                break;

              case SYM_PREDICATE_X:
                TYPE_SET(typeset, REB_TS_PREDICATE);
                break;

              default:
                fail ("Unknown fake type constraint!");
            }
        }
        else
            fail (Error_Bad_Value_Core(maybe_word, specifier));

        // !!! Review erroring policy--should probably not just be ignoring
        // things that aren't recognized here (!)
    }

    return true;
}


//
//  MAKE_Typeset: C
//
REB_R MAKE_Typeset(
    REBVAL *out,
    enum Reb_Kind kind,
    const REBVAL *opt_parent,
    const REBVAL *arg
){
    assert(kind == REB_TYPESET);
    if (opt_parent)
        fail (Error_Bad_Make_Parent(kind, opt_parent));

    if (IS_TYPESET(arg))
        return Move_Value(out, arg);

    if (!IS_BLOCK(arg)) goto bad_make;

    Init_Typeset(out, 0);
    Add_Typeset_Bits_Core(out, VAL_ARRAY_AT(arg), VAL_SPECIFIER(arg));
    return out;

  bad_make:
    fail (Error_Bad_Make(REB_TYPESET, arg));
}


//
//  TO_Typeset: C
//
REB_R TO_Typeset(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    return MAKE_Typeset(out, kind, nullptr, arg);
}


//
//  Typeset_To_Array: C
//
// Converts typeset value to a block of datatypes, no order is guaranteed.
//
REBARR *Typeset_To_Array(const REBVAL *tset)
{
    REBDSP dsp_orig = DSP;

    REBINT n;
    for (n = 1; n < REB_MAX; ++n) {
        if (TYPE_CHECK(tset, cast(enum Reb_Kind, n))) {
            if (n == REB_NULL) {
                //
                // !!! A BLANK! value is currently supported in typesets to
                // indicate that they take optional values.  This may wind up
                // as a feature of MAKE ACTION! only.
                //
                Init_Blank(DS_PUSH());
            }
            else
                Init_Builtin_Datatype(DS_PUSH(), cast(enum Reb_Kind, n));
        }
    }

    return Pop_Stack_Values(dsp_orig);
}


//
//  MF_Typeset: C
//
void MF_Typeset(REB_MOLD *mo, REBCEL(const*) v, bool form)
{
    REBINT n;

    if (not form) {
        Pre_Mold(mo, v);  // #[typeset! or make typeset!
        Append_Codepoint(mo->series, '[');
    }

    // Convert bits to type name strings.  Note that "endability" and
    // "optionality" are not really good fits for things in a typeset, as no
    // "type" exists for their bits.  However, you can get them if you say
    // `TYPESETS OF` on an action.  This should be thought about.

    if (TYPE_CHECK(v, REB_0_END))
        Append_Ascii(mo->series, "<end> ");

    STATIC_ASSERT(REB_NULL == 1);
    if (TYPE_CHECK(v, REB_NULL))
        Append_Ascii(mo->series, "<opt> ");

    // !!! What about REB_TS_SKIPPABLE and other parameter properties, that
    // don't really fit into "types", but you can get with TYPESETS OF action?

    for (n = REB_NULL + 1; n < REB_MAX; n++) {
        enum Reb_Kind kind = cast(enum Reb_Kind, n);
        if (TYPE_CHECK(v, kind)) {
            if (kind == REB_CUSTOM) {
                //
                // !!! Typesets have not been worked out yet to handle type
                // checking for custom datatypes, as they only support 64 bits
                // of information at the moment.  Hack around it for now.
                //
                Append_Ascii(mo->series, "#[datatype! custom!]");
            }
            else
                Mold_Value(mo, Datatype_From_Kind(kind));
            Append_Codepoint(mo->series, ' ');
        }
    }
    Trim_Tail(mo, ' ');

    if (not form) {
        Append_Codepoint(mo->series, ']');
        End_Mold(mo);
    }
}


//
//  REBTYPE: C
//
REBTYPE(Typeset)
{
    REBVAL *v = D_ARG(1);

    switch (VAL_WORD_SYM(verb)) {
      case SYM_FIND: {
        INCLUDE_PARAMS_OF_FIND;
        UNUSED(ARG(series));  // covered by `v`

        UNUSED(REF(only));  // !!! tolerate, even though ignored?
        UNUSED(REF(case));  // !!! tolerate, even though ignored?

        if (
            REF(part) or REF(skip) or REF(tail) or REF(match)
            or REF(reverse) or REF(last)
        ){
            fail (Error_Bad_Refines_Raw());
        }

        REBVAL *pattern = ARG(pattern);
        if (not IS_DATATYPE(pattern))
            fail (pattern);

        if (TYPE_CHECK(v, VAL_TYPE_KIND(pattern)))
            return Init_True(D_OUT);

        return nullptr; }

      case SYM_UNIQUE:
        RETURN (v);  // typesets unique by definition

      case SYM_INTERSECT:
      case SYM_UNION:
      case SYM_DIFFERENCE:
      case SYM_EXCLUDE: {
        REBVAL *arg = D_ARG(2);

        if (IS_DATATYPE(arg))
            Init_Typeset(arg, FLAGIT_KIND(VAL_TYPE(arg)));
        else if (not IS_TYPESET(arg))
            fail (arg);

        switch (VAL_WORD_SYM(verb)) {
          case SYM_UNION:
            VAL_TYPESET_LOW_BITS(v) |= VAL_TYPESET_LOW_BITS(arg);
            VAL_TYPESET_HIGH_BITS(v) |= VAL_TYPESET_HIGH_BITS(arg);
            break;
        
          case SYM_INTERSECT:
            VAL_TYPESET_LOW_BITS(v) &= VAL_TYPESET_LOW_BITS(arg);
            VAL_TYPESET_HIGH_BITS(v) &= VAL_TYPESET_HIGH_BITS(arg);
            break;

          case SYM_DIFFERENCE:
            VAL_TYPESET_LOW_BITS(v) ^= VAL_TYPESET_LOW_BITS(arg);
            VAL_TYPESET_HIGH_BITS(v) ^= VAL_TYPESET_HIGH_BITS(arg);
            break;

          case SYM_EXCLUDE:
            VAL_TYPESET_LOW_BITS(v) &= ~VAL_TYPESET_LOW_BITS(arg);
            VAL_TYPESET_HIGH_BITS(v) &= ~VAL_TYPESET_HIGH_BITS(arg);
            break;

          default:
            assert(false);
        }

        RETURN (v); }

      case SYM_COMPLEMENT: {
        VAL_TYPESET_LOW_BITS(v) = ~VAL_TYPESET_LOW_BITS(v);
        VAL_TYPESET_HIGH_BITS(v) = ~VAL_TYPESET_HIGH_BITS(v);
        RETURN (v); }

      case SYM_COPY:
        RETURN (v);

      default:
        break;
    }

    return R_UNHANDLED;
}
