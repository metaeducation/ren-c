//
//  File: %c-typechecker.c
//  Summary: "Function generator for an optimized typechecker"
//  Section: datatypes
//  Project: "Ren-C Language Interpreter and Run-time Environment"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2016-2022 Ren-C Open Source Contributors
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the GNU Lesser General Public License (LGPL), Version 3.0.
// You may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.en.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Making a typechecker can be easy:
//
//     >> integer?: func [v [any-value!]] [integer! = kind of :v]
//
//     >> integer? 10
//     == ~true~  ; isotope
//
//     >> integer? <foo>
//     == ~false~  ; isotope
//
// But given that it is done so often, it's more efficient to have a custom
// dispatcher for making a typechecker:
//
//     >> integer?: typechecker &integer
//
// This makes a near-native optimized version of the type checker which uses
// a custom dispatcher.  Additionally, when used in a type constraint the
// dispatcher can be recognized to bypass an interpreted function call
// entirely to check the type.
//

#include "sys-core.h"

enum {
    IDX_TYPECHECKER_TYPE = 1,  // datatype or typeset to check
    IDX_TYPECHECKER_MAX
};


//
//  typecheck-internal?: native [
//
//      return: [logic?]
//      optional
//  ]
//
DECLARE_NATIVE(typecheck_internal_q)
//
// Note: This prototype is used by all TYPECHECKER instances.  (It steals the
// paramlist from this native.)
{
    INCLUDE_PARAMS_OF_TYPECHECK_INTERNAL_Q;

    UNUSED(ARG(optional));
    panic (nullptr);
}


//
//  Datatype_Checker_Dispatcher: C
//
// Dispatcher used by TYPECHECKER generator for when argument is a datatype.
//
Bounce Datatype_Checker_Dispatcher(Frame(*) frame_)
{
    Frame(*) f = frame_;

    Array(*) details = ACT_DETAILS(FRM_PHASE(f));
    assert(ARR_LEN(details) == IDX_TYPECHECKER_MAX);

    REBVAL *datatype = DETAILS_AT(details, IDX_TYPECHECKER_TYPE);

    assert(KEY_SYM(ACT_KEY(FRM_PHASE(f), 1)) == SYM_RETURN);  // skip arg 1

    return Init_Logic(  // otherwise won't be equal to any custom type
        OUT,
        VAL_TYPE(FRM_ARG(f, 2)) == VAL_TYPE_KIND(datatype)
    );
}


//
//  Typeset_Checker_Dispatcher: C
//
// Dispatcher used by TYPECHECKER generator for when argument is a typeset.
//
Bounce Typeset_Checker_Dispatcher(Frame(*) frame_)
{
    Frame(*) f = frame_;

    Array(*) details = ACT_DETAILS(FRM_PHASE(f));
    assert(ARR_LEN(details) == IDX_TYPECHECKER_MAX);

    REBVAL *typeset_index = DETAILS_AT(details, IDX_TYPECHECKER_TYPE);
    assert(IS_INTEGER(typeset_index));
    Index n = VAL_INT32(typeset_index);

    assert(KEY_SYM(ACT_KEY(FRM_PHASE(f), 1)) == SYM_RETURN);  // skip arg 1

    REBU64 typeset = Typesets[n];
    enum Reb_Kind kind = VAL_TYPE(FRM_ARG(f, 2));
    return Init_Logic(OUT, FLAGIT_KIND(kind) & typeset);
}


//
//  typechecker: native [
//
//  {Generator for an optimized typechecking ACTION!}
//
//      return: [activation?]
//      type [type-word! integer!]
//  ]
//
DECLARE_NATIVE(typechecker)
{
    INCLUDE_PARAMS_OF_TYPECHECKER;

    REBVAL *type = ARG(type);

    Action(*) typechecker = Make_Action(
        ACT_PARAMLIST(VAL_ACTION(Lib(TYPECHECK_INTERNAL_Q))),
        nullptr,  // no partials
        IS_TYPE_WORD(type)
            ? &Datatype_Checker_Dispatcher
            : &Typeset_Checker_Dispatcher,
        IDX_TYPECHECKER_MAX  // details array capacity
    );
    Copy_Cell(ARR_AT(ACT_DETAILS(typechecker), IDX_TYPECHECKER_TYPE), type);

    return Init_Activation(OUT, typechecker, ANONYMOUS, UNBOUND);
}


//
//  Typecheck_Value: C
//
// Ren-C has eliminated the concept of TYPESET!, instead gaining behaviors
// for TYPE-BLOCK! and TYPE-GROUP!.
//
bool Typecheck_Value(
    Cell(const*) tests,  // can be BLOCK!, TYPE-BLOCK!, GROUP!, TYPE-GROUP!
    REBSPC *tests_specifier,
    Cell(const*) v,
    REBSPC *v_specifier
){
    DECLARE_LOCAL (spare);  // !!! stackful

    Cell(const*) tail;
    Cell(const*) item;
    bool match_all;
    if (IS_BLOCK(tests) or IS_TYPE_BLOCK(tests)) {
        item = VAL_ARRAY_AT(&tail, tests);
        match_all = false;
    }
    else if (IS_GROUP(tests) or IS_TYPE_GROUP(tests)) {
        item = VAL_ARRAY_AT(&tail, tests);
        match_all = true;
    }
    else if (IS_PARAMETER(tests)) {
        Array(const*) array = try_unwrap(VAL_PARAMETER_ARRAY(tests));
        if (array == nullptr)
            return true;  // implicitly all is permitted
        item = ARR_HEAD(array);
        tail = ARR_TAIL(array);
        match_all = false;
    }
    else if (IS_TYPE_WORD(tests)) {
        item = tests;
        tail = tests + 1;
        match_all = true;
    }
    else {
        assert(false);
        fail ("Bad test passed to Typecheck_Value");
    }

    for (; item != tail; ++item) {
        //
        // !!! Ultimately, we'll enable literal comparison for quoted/quasi
        // items.  For the moment just try quasi-words for isotopes.
        //
        if (IS_QUASI(item)) {
            if (HEART_BYTE(item) == REB_VOID) {
                if (Is_Quasi_Void(item))
                    goto test_succeeded;
                goto test_failed;
            }

            if (HEART_BYTE(item) != REB_WORD)
                fail (item);

            if (not Is_Isoword(v))
                continue;
            if (VAL_WORD_SYMBOL(v) == VAL_WORD_SYMBOL(item))
                goto test_succeeded;
            goto test_failed;
        }

        enum Reb_Kind kind;
        Cell(const*) test;
        if (IS_WORD(item)) {
            test = Lookup_Word_May_Fail(item, tests_specifier);
            kind = VAL_TYPE(test);  // e.g. TYPE-BLOCK! <> BLOCK!
        }
        else {
            test = item;
            if (IS_BLOCK(test))
                kind = REB_TYPE_BLOCK;
            else if (IS_GROUP(test))
                kind = REB_TYPE_GROUP;
            else
                kind = VAL_TYPE(test);
        }

        if (Is_Activation(test))
            goto run_activation;

        switch (kind) {
          run_activation:
          case REB_ACTION: {
            Action(*) action = VAL_ACTION(test);

            // NULL? and VOID? do not have type specs on their argument,
            // because if they did they would have to mention <opt> and <void>
            // and this would lead to an infinite recursion if called here.
            //
            // But we still speedup the checking to avoid needing a function
            // call.  This could be generalized, where typecheckers are
            // associated with internal function pointers for testing...so
            // no actual frame needed to be built for any arity-1 and
            // logic-returning native.

            if (action == VAL_ACTION(Lib(NULL_Q))) {
                if (Is_Nulled(v))
                    goto test_succeeded;
                goto test_failed;
            }

            if (action == VAL_ACTION(Lib(VOID_Q))) {
                if (Is_Void(v))
                    goto test_succeeded;
                goto test_failed;
            }

            // Here we speedup the typeset checking.  It may be that the
            // acceleration could be unified with a function pointer method
            // if we are willing to make functions for checking each typeset
            // instead of using a table.
            //
            if (ACT_DISPATCHER(action) == &Typeset_Checker_Dispatcher) {
                Index n = VAL_INT32(
                    DETAILS_AT(ACT_DETAILS(action), IDX_TYPECHECKER_TYPE)
                );
                REBU64 bits = Typesets[n];
                enum Reb_Kind k;
                if (Is_Isotope(v) and Is_Isotope_Unstable(v))
                    k = REB_ISOTOPE;
                else
                    k = VAL_TYPE(v);
                if (bits & FLAGIT_KIND(k))
                    goto test_succeeded;
                goto test_failed;
            }

            if (ACT_DISPATCHER(action) == &Datatype_Checker_Dispatcher) {
                Value(*) type_word = DETAILS_AT(
                    ACT_DETAILS(action),
                    IDX_TYPECHECKER_TYPE
                );
                enum Reb_Kind k;
                if (Is_Isotope(v) and Is_Isotope_Unstable(v))
                    k = REB_ISOTOPE;
                else
                    k = VAL_TYPE(v);
                if (k == VAL_TYPE_KIND(type_word))
                    goto test_succeeded;
                goto test_failed;
            }

            Flags flags = 0;
            Frame(*) f = Make_End_Frame(
                FLAG_STATE_BYTE(ST_ACTION_TYPECHECKING) | flags
            );
            Push_Action(f, VAL_ACTION(test), VAL_ACTION_BINDING(test));
            Begin_Prefix_Action(f, VAL_ACTION_LABEL(test));

            const REBKEY *key = f->u.action.key;
            const REBPAR *param = f->u.action.param;
            REBVAL *arg = f->u.action.arg;
            for (; key != f->u.action.key_tail; ++key, ++param, ++arg) {
                if (Is_Specialized(param))
                    Copy_Cell(arg, param);
                else
                    Finalize_None(arg);
                assert(Is_Stable(arg));
            }

            arg = First_Unspecialized_Arg(&param, f);
            if (not arg)
                fail ("Type predicate doesn't take an argument");

            Derelativize(arg, v, v_specifier);  // do not decay, see [4]

            if (VAL_PARAM_CLASS(param) == PARAM_CLASS_META)
                Meta_Quotify(arg);

            if (not Typecheck_Coerce_Argument(param, arg)) {
                Drop_Action(f);
                if (match_all)
                    return false;
                continue;
            }

            Push_Frame(spare, f);

            if (Trampoline_With_Top_As_Root_Throws())
                fail (Error_No_Catch_For_Throw(TOP_FRAME));

            Drop_Frame(f);

            if (not IS_LOGIC(spare))
                fail ("Type Predicates Must Return LOGIC!");

            if (not VAL_LOGIC(spare))
                goto test_failed;
            break; }

          case REB_TYPE_BLOCK:
          case REB_TYPE_GROUP: {
            REBSPC *subspecifier = Derive_Specifier(tests_specifier, test);
            if (not Typecheck_Value(test, subspecifier, v, v_specifier))
                goto test_failed;
            break; }

          case REB_QUOTED:
          case REB_QUASI: {
            fail ("QUOTED! and QUASI! not currently supported in TYPE-XXX!"); }

          case REB_PARAMETER: {
            if (not Typecheck_Value(test, SPECIFIED, v, v_specifier))
                goto test_failed;
            break; }

          case REB_TYPE_WORD: {
            enum Reb_Kind k;
            if (Is_Isotope(v) and Is_Isotope_Unstable(v))
                k = REB_ISOTOPE;
            else
                k = VAL_TYPE(v);
            if (VAL_TYPE_KIND(test) != k)
                goto test_failed;
            break; }

          case REB_TAG: {
            bool strict = false;

            if (0 == CT_String(test, Root_Opt_Tag, strict)) {
                if (not Is_Nulled(v))
                    goto test_failed;
            }
            if (0 == CT_String(test, Root_Void_Tag, strict)) {
                if (not Is_Void(v))
                    goto test_failed;
            }
            break; }  // currently, ignore all other tags

          default:
            fail ("Invalid element in TYPE-GROUP!");
        }
        goto test_succeeded;

      test_succeeded:
        if (not match_all)
            return true;
        continue;

      test_failed:
        if (match_all)
            return false;
        continue;
    }

    if (match_all)
        return true;
    return false;
}


//
//  Typecheck_Coerce_Argument: C
//
// This does extra typechecking pertinent to function parameters, compared to
// the basic type checking.
//
// 1. !!! Should explicit mutability override, so people can say things
//    like `foo: func [...] mutable [...]` ?  This seems bad, because the
//    contract of the function hasn't been "tweaked" with reskinning.
//
bool Typecheck_Coerce_Argument(
    const REBPAR *param,
    Value(*) arg  // need mutability for coercion
){
    if (GET_PARAM_FLAG(param, CONST))
        Set_Cell_Flag(arg, CONST);  // mutability override?  see [1]

    if (
        GET_PARAM_FLAG(param, REFINEMENT)
        or GET_PARAM_FLAG(param, SKIPPABLE)
    ){
        if (Is_Nulled(arg))  // nulls always legal...means refinement not used
            return true;

        if (Is_Parameter_Unconstrained(param))  // no-arg refinement
            return Is_Blackhole(arg);  // !!! Error_Bad_Argless_Refine(key)
    }

    bool coerced = false;

    // We do an adjustment of the argument to accommodate meta parameters,
    // which check the unquoted type.
    //
    bool unquoted = false;

    if (VAL_PARAM_CLASS(param) == PARAM_CLASS_META) {
        if (Is_Nulled(arg))
            return GET_PARAM_FLAG(param, ENDABLE);

        if (not IS_QUASI(arg) and not IS_QUOTED(arg))
            return false;

        Meta_Unquotify_Undecayed(arg);  // temporary adjustment (easiest option)
        unquoted = true;
    }
    else if (VAL_PARAM_CLASS(param) == PARAM_CLASS_RETURN) {
        unquoted = false;
    }
    else {
        unquoted = false;

        if (not Is_Stable(arg))
            goto do_coercion;
    }

  typecheck_again:

    if (TYPE_CHECK(param, arg))
        goto return_true;

    if (not coerced) {

      do_coercion:

        if (Is_Activation(arg)) {
            mutable_QUOTE_BYTE(arg) = UNQUOTED_1;
            coerced = true;
            goto typecheck_again;
        }

        if (Is_Raised(arg))
            goto return_false;

        if (Is_Nihil(arg))
            goto return_false;  // can't decay

        if (Is_Isotope(arg) and Is_Isotope_Unstable(arg)) {
            Decay_If_Unstable(arg);
            coerced = true;
            goto typecheck_again;
        }
    }

  return_false:

    if (unquoted)
        Meta_Quotify(arg);

    return false;

  return_true:

    if (unquoted)
        Meta_Quotify(arg);

    if (not Is_Stable(arg))
        assert(VAL_PARAM_CLASS(param) == PARAM_CLASS_RETURN);

    return true;
}
