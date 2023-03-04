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
//  CT_Typeset: C
//
REBINT CT_Typeset(noquote(Cell(const*)) a, noquote(Cell(const*)) b, bool strict)
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
    REBINT id;
    for (id = SYM_ANY_VALUE_X; id != SYM_DATATYPES; ++id) {
        REBINT n = id - SYM_ANY_VALUE_X;

        StackIndex base = TOP_INDEX;
        REBINT kind;
        for (kind = 0; kind < REB_MAX; ++kind) {
            if (not (Typesets[n] & FLAGIT_KIND(kind)))
                continue;

            Init_Any_Word_Bound(
                PUSH(),
                REB_WORD,
                Canon_Symbol(cast(SymId, kind)),
                Lib_Context,
                INDEX_ATTACHED  // !!! should this be INDEX_PATCHED?
            );
        }

        Array(*) a = Pop_Stack_Values_Core(base, NODE_FLAG_MANAGED);
        Init_Typeset(Force_Lib_Var(cast(SymId, id)), a);
    }

    assert(Typesets[id - SYM_ANY_VALUE_X] == 0);  // table ends in zero
}


//
//  Shutdown_Typesets: C
//
void Shutdown_Typesets(void)
{
    int i;
    for (i = 0; i < 1; ++i) {
        const REBTYP** custom = c_cast(const REBTYP**, &PG_Extension_Types[i]);
        *custom = nullptr;  // managed binary node, remove reference
    }
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
Array(*) Add_Typeset_Bits_Core(
    Flags* flags,
    Cell(const*) head,
    Cell(const*) tail,
    REBSPC *specifier
){
    StackIndex base = TOP_INDEX;
    *flags = 0;

    Cell(const*) maybe_word = head;
    for (; maybe_word != tail; ++maybe_word) {
        Cell(const*) item;
        if (IS_QUASI(maybe_word)) {
            if (HEART_BYTE(maybe_word) != REB_WORD)
                fail ("QUASI! must be of WORD! in typeset spec");

            // We suppress isotopic decay on the parameter only if it actually
            // requests seeing isotopic words, potentially transitively.
            //
            switch (VAL_WORD_ID(maybe_word)) {
              case SYM_WORD_X :
              case SYM_ANY_WORD_X :
              case SYM_ANY_VALUE_X :
              case SYM_ANY_UTF8_X :
                *flags |= PARAM_FLAG_ISOTOPES_OKAY;
                *flags |= PARAM_FLAG_NO_ISOTOPE_DECAY;
                break;

              default:
                *flags |= PARAM_FLAG_ISOTOPES_OKAY;
                break;
            }
            continue;
        }

        if (IS_WORD(maybe_word))
            item = Lookup_Word_May_Fail(maybe_word, specifier);
        else
            item = maybe_word;  // wasn't variable

        if (IS_TAG(item)) {
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
                *flags |= PARAM_FLAG_VARIADIC;
            }
            else if (0 == CT_String(item, Root_End_Tag, strict)) {
                *flags |= PARAM_FLAG_ENDABLE;
                Init_Any_Word_Bound(
                    PUSH(),
                    REB_WORD,
                    Canon(NULL),
                    Lib_Context,
                    INDEX_ATTACHED
                );
            }
            else if (0 == CT_String(item, Root_Maybe_Tag, strict)) {
                *flags |= PARAM_FLAG_NOOP_IF_VOID;
            }
            else if (0 == CT_String(item, Root_Opt_Tag, strict)) {
                Init_Any_Word_Bound(
                    PUSH(),
                    REB_WORD,
                    Canon(NULL),
                    Lib_Context,
                    INDEX_ATTACHED
                );
            }
            else if (0 == CT_String(item, Root_Void_Tag, strict)) {
                *flags |= PARAM_FLAG_VANISHABLE;
            }
            else if (0 == CT_String(item, Root_Fail_Tag, strict)) {
                *flags |= PARAM_FLAG_WANT_FAILURES;
            }
            else if (0 == CT_String(item, Root_Pack_Tag, strict)) {
                *flags |= PARAM_FLAG_WANT_PACKS;
            }
            else if (0 == CT_String(item, Root_Skip_Tag, strict)) {
               /* if (VAL_PARAM_CLASS(typeset) != PARAM_CLASS_HARD)
                    fail ("Only hard-quoted parameters are <skip>-able"); */

                *flags |= PARAM_FLAG_SKIPPABLE;
                *flags |= PARAM_FLAG_ENDABLE; // skip => null
                Init_Any_Word_Bound(
                    PUSH(),
                    REB_WORD,
                    Canon(NULL),
                    Lib_Context,
                    INDEX_ATTACHED
                );
            }
            else if (0 == CT_String(item, Root_Const_Tag, strict)) {
                *flags |= PARAM_FLAG_CONST;
            }
        }
        else if (IS_DATATYPE(item) or IS_TYPESET(item)) {
            Derelativize(PUSH(), maybe_word, specifier);
        }
        else if (IS_META_WORD(item)) {  // see Startup_Fake_Type_Constraint()
            Derelativize(PUSH(), maybe_word, specifier);
        }
        else
            fail (Error_Bad_Value(maybe_word));

        // !!! Review erroring policy--should probably not just be ignoring
        // things that aren't recognized here (!)
    }

    return Pop_Stack_Values_Core(base, NODE_FLAG_MANAGED);
}


//
//  MAKE_Typeset: C
//
Bounce MAKE_Typeset(
    Frame(*) frame_,
    enum Reb_Kind kind,
    option(const REBVAL*) parent,
    const REBVAL *arg
){
    assert(kind == REB_TYPESET);
    if (parent)
        return RAISE(Error_Bad_Make_Parent(kind, unwrap(parent)));

    if (IS_TYPESET(arg))
        return Copy_Cell(OUT, arg);

    if (!IS_BLOCK(arg)) goto bad_make;

  blockscope {
    Cell(const*) tail;
    Cell(const*) at = VAL_ARRAY_AT(&tail, arg);
    Init_Typeset(OUT, 0);
    Flags flags;
    INIT_VAL_TYPESET_ARRAY(OUT,
        Add_Typeset_Bits_Core(
            &flags,
            at,
            tail,
            VAL_SPECIFIER(arg)
        )
    );
    return OUT;
  }

  bad_make:

    return RAISE(Error_Bad_Make(REB_TYPESET, arg));
}


//
//  TO_Typeset: C
//
Bounce TO_Typeset(Frame(*) frame_, enum Reb_Kind kind, const REBVAL *arg)
{
    return MAKE_Typeset(frame_, kind, nullptr, arg);
}


//
//  Typeset_To_Array: C
//
// Converts typeset value to a block of datatypes, no order is guaranteed.
//
// !!! Typesets are likely to be scrapped in their current form; this is just
// here to try and keep existing code running for now.
//
// https://forum.rebol.info/t/the-typeset-representation-problem/1300
//
Array(*) Typeset_To_Array(const REBVAL *tset)
{
    return Copy_Array_Shallow(VAL_TYPESET_ARRAY(tset), SPECIFIED);
}


//
//  MF_Typeset: C
//
void MF_Typeset(REB_MOLD *mo, noquote(Cell(const*)) v, bool form)
{
    if (not form) {
        Pre_Mold(mo, v);  // #[typeset! or make typeset!
    }

    DECLARE_LOCAL(temp);
    Init_Group(temp, VAL_TYPESET_ARRAY(v));
    PUSH_GC_GUARD(temp);
    Mold_Or_Form_Value(mo, temp, form);
    DROP_GC_GUARD(temp);

    if (not form) {
        End_Mold(mo);
    }
}


//
//  REBTYPE: C
//
REBTYPE(Typeset)
{
    REBVAL *v = D_ARG(1);

    switch (ID_OF_SYMBOL(verb)) {
      case SYM_FIND: {
        INCLUDE_PARAMS_OF_FIND;
        UNUSED(ARG(series));  // covered by `v`
        UNUSED(ARG(tail));  // not supported

        UNUSED(REF(case));  // !!! tolerate, even though ignored?

        if (REF(part) or REF(skip) or REF(match))
            fail (Error_Bad_Refines_Raw());

        REBVAL *pattern = ARG(pattern);
        if (Is_Isotope(pattern))
            fail (pattern);

        if (not IS_DATATYPE(pattern))
            fail (pattern);

        if (TYPE_CHECK(v, pattern))
            return Init_True(OUT);

        return nullptr; }

      case SYM_UNIQUE:
      case SYM_INTERSECT:
      case SYM_UNION:
      case SYM_DIFFERENCE:
      case SYM_EXCLUDE:
      case SYM_COMPLEMENT:
        fail ("TYPESET! INTERSECT/UNION/etc. currently disabled");

      case SYM_COPY:
        return COPY(v);

      default:
        break;
    }

    return BOUNCE_UNHANDLED;
}
