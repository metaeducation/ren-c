//
//  File: %t-function.c
//  Summary: "function related datatypes"
//  Section: datatypes
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
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

#include "sys-core.h"

static REBOOL Same_Func(const RELVAL *val, const RELVAL *arg)
{
    assert(IS_FUNCTION(val) && IS_FUNCTION(arg));

    if (VAL_FUNC_PARAMLIST(val) == VAL_FUNC_PARAMLIST(arg)) {
        assert(VAL_FUNC_DISPATCHER(val) == VAL_FUNC_DISPATCHER(arg));
        assert(VAL_FUNC_BODY(val) == VAL_FUNC_BODY(arg));

        // All functions that have the same paramlist are not necessarily the
        // "same function".  For instance, every RETURN shares a common
        // paramlist, but the binding is different in the REBVAL instances
        // in order to know where to "exit from".

        return LOGICAL(VAL_BINDING(val) == VAL_BINDING(arg));
    }

    return FALSE;
}


//
//  CT_Function: C
//
REBINT CT_Function(const RELVAL *a, const RELVAL *b, REBINT mode)
{
    if (mode >= 0) return Same_Func(a, b) ? 1 : 0;
    return -1;
}


//
//  MAKE_Function: C
//
// For REB_FUNCTION and "make spec", there is a function spec block and then
// a block of Rebol code implementing that function.  In that case we expect
// that `def` should be:
//
//     [[spec] [body]]
//
// With REB_COMMAND, the code is implemented via a C DLL, under a system of
// APIs that pre-date Rebol's open sourcing and hence Ren/C:
//
//     [[spec] extension command-num]
//
// See notes in Make_Command() regarding that mechanism and meaning.
//
void MAKE_Function(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    assert(kind == REB_FUNCTION);
    UNUSED(kind);

    if (
        !IS_BLOCK(arg)
        || VAL_LEN_AT(arg) != 2
        || !IS_BLOCK(VAL_ARRAY_AT(arg))
        || !IS_BLOCK(VAL_ARRAY_AT(arg) + 1)
    ){
        fail (Error_Bad_Make(REB_FUNCTION, arg));
    }

    DECLARE_LOCAL (spec);
    Derelativize(spec, VAL_ARRAY_AT(arg), VAL_SPECIFIER(arg));

    DECLARE_LOCAL (body);
    Derelativize(body, VAL_ARRAY_AT(arg) + 1, VAL_SPECIFIER(arg));

    // Spec-constructed functions do *not* have definitional returns
    // added automatically.  They are part of the generators.  So the
    // behavior comes--as with any other generator--from the projected
    // code (though round-tripping it via text is not possible in
    // general in any case due to loss of bindings.)
    //
    REBFUN *fun = Make_Interpreted_Function_May_Fail(
        spec, body, MKF_ANY_VALUE
    );

    Move_Value(out, FUNC_VALUE(fun));
}


//
//  TO_Function: C
//
// `to function! 'x` might be an interesting optimized 0-arity function
// generator, which made a function that returned that value every time you
// called it.  Generalized alternative would be like `does [quote x]`,
// which would be slower to generate the function and slower to run.
//
void TO_Function(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    assert(kind == REB_FUNCTION);
    UNUSED(kind);

    UNUSED(out);

    fail (arg);
}


//
//  MF_Function: C
//
void MF_Function(REB_MOLD *mo, const RELVAL *v, REBOOL form)
{
    UNUSED(form);

    Pre_Mold(mo, v);

    Append_Codepoint(mo->series, '[');

    // !!! The system is no longer keeping the spec of functions, in order
    // to focus on a generalized "meta info object" service.  MOLD of
    // functions temporarily uses the word list as a substitute (which
    // drops types)
    //
    REBARR *words_list = List_Func_Words(v, TRUE); // show pure locals
    Mold_Array_At(mo, words_list, 0, 0);
    Free_Array(words_list);

    if (IS_FUNCTION_INTERPRETED(v)) {
        //
        // MOLD is an example of user-facing code that needs to be complicit
        // in the "lie" about the effective bodies of the functions made
        // by the optimized generators FUNC and PROC...

        REBOOL is_fake;
        REBARR *body = Get_Maybe_Fake_Func_Body(&is_fake, const_KNOWN(v));

        Mold_Array_At(mo, body, 0, 0);

        if (is_fake)
            Free_Array(body); // was shallow copy
    }
    else if (IS_FUNCTION_SPECIALIZER(v)) {
        //
        // !!! Interim form of looking at specialized functions... show
        // the frame
        //
        //     >> source first
        //     first: make function! [[aggregate index] [
        //         aggregate: $void
        //         index: 1
        //     ]]
        //
        REBVAL *exemplar = KNOWN(VAL_FUNC_BODY(v));
        Mold_Value(mo, exemplar);
    }

    Append_Codepoint(mo->series, ']');
    End_Mold(mo);
}


//
//  REBTYPE: C
//
REBTYPE(Function)
{
    REBVAL *value = D_ARG(1);
    REBVAL *arg = D_ARGC > 1 ? D_ARG(2) : NULL;

    switch (action) {
    case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;

        UNUSED(PAR(value));
        if (REF(part)) {
            UNUSED(ARG(limit));
            fail (Error_Bad_Refines_Raw());
        }
        if (REF(types)) {
            UNUSED(ARG(kinds));
            fail (Error_Bad_Refines_Raw());
        }
        if (REF(deep)) {
            // !!! always "deep", allow it?
        }

        // Copying functions creates another handle which executes the same
        // code, yet has a distinct identity.  This means it would not be
        // HIJACK'd if the function that it was copied from was.

        REBARR *proxy_paramlist = Copy_Array_Deep_Managed(
            VAL_FUNC_PARAMLIST(value),
            SPECIFIED // !!! Note: not actually "deep", just typesets
        );
        ARR_HEAD(proxy_paramlist)->payload.function.paramlist
            = proxy_paramlist;
        MISC(proxy_paramlist).meta = VAL_FUNC_META(value);
        SET_SER_FLAG(proxy_paramlist, ARRAY_FLAG_PARAMLIST);

        // If the function had code, then that code will be bound relative
        // to the original paramlist that's getting hijacked.  So when the
        // proxy is called, we want the frame pushed to be relative to
        // whatever underlied the function...even if it was foundational
        // so `underlying = VAL_FUNC(value)`

        REBFUN *proxy = Make_Function(
            proxy_paramlist,
            FUNC_DISPATCHER(VAL_FUNC(value)),
            FUNC_FACADE(VAL_FUNC(value)), // can reuse the facade
            FUNC_EXEMPLAR(VAL_FUNC(value)) // not changing the specialization
        );

        // A new body_holder was created inside Make_Function().  Rare case
        // where we can bit-copy a possibly-relative value.
        //
        Blit_Cell(FUNC_BODY(proxy), VAL_FUNC_BODY(value));

        Move_Value(D_OUT, FUNC_VALUE(proxy));
        D_OUT->extra.binding = VAL_BINDING(value);
        return R_OUT; }

    case SYM_REFLECT: {
        REBSYM sym = VAL_WORD_SYM(arg);

        switch (sym) {

        case SYM_CONTEXT: {
            if (Get_Context_Of(D_OUT, value))
                return R_OUT;
            return R_BLANK; }

        case SYM_WORDS:
            Init_Block(D_OUT, List_Func_Words(value, FALSE)); // no locals
            return R_OUT;

        case SYM_BODY:
            //
            // A Hijacker may or may not need to splice itself in with a
            // dispatcher.  So if it does, bypass it to get to the real
            // function implementation.
            //
            while (IS_FUNCTION_HIJACKER(value))
                value = KNOWN(VAL_FUNC_BODY(value));

            if (IS_FUNCTION_INTERPRETED(value)) {
                //
                // BODY-OF is an example of user-facing code that needs to be
                // complicit in the "lie" about the effective bodies of the
                // functions made by the optimized generators FUNC and PROC.
                //
                // Note that since the function body contains relative arrays
                // and words, there needs to be some frame to specify them
                // before a specific REBVAL can be made.  Usually that's the
                // frame of the running instance of the function...but because
                // we're reflecting data out of it, we have to either unbind
                // them or make up a frame.  Making up a frame that acts like
                // it's off the stack and the variables are dead is easiest
                // for now...but long term perhaps unbinding them is better,
                // though this is "more informative".  See #2221.

                REBOOL is_fake;
                REBARR *body = Get_Maybe_Fake_Func_Body(&is_fake, value);
                Init_Block(
                    D_OUT,
                    Copy_Array_Deep_Managed(
                        body,
                        AS_SPECIFIER(
                            Make_Expired_Frame_Ctx_Managed(VAL_FUNC(value))
                        )
                    )
                );

                if (is_fake) Free_Array(body); // was shallow copy
                return R_OUT;
            }

            // For other function types, leak internal guts and hope for
            // the best, temporarily.
            //
            if (IS_BLOCK(VAL_FUNC_BODY(value))) {
                Init_Any_Array(
                    D_OUT,
                    REB_BLOCK,
                    Copy_Array_Deep_Managed(
                        VAL_ARRAY(VAL_FUNC_BODY(value)), SPECIFIED
                    )
                );
            }
            else {
                Init_Blank(D_OUT);
            }
            return R_OUT;

        case SYM_TYPES: {
            REBARR *copy = Make_Array(VAL_FUNC_NUM_PARAMS(value));
            REBVAL *param;
            REBVAL *typeset;

            // The typesets have a symbol in them for the parameters, and
            // ordinary typesets aren't supposed to have it--that's a
            // special feature for object keys and paramlists!  So clear
            // that symbol out before giving it back.
            //
            param = VAL_FUNC_PARAMS_HEAD(value);
            typeset = SINK(ARR_HEAD(copy));
            for (; NOT_END(param); param++, typeset++) {
                assert(VAL_PARAM_SPELLING(param) != NULL);
                Move_Value(typeset, param);
                INIT_TYPESET_NAME(typeset, NULL);
            }
            TERM_ARRAY_LEN(copy, VAL_FUNC_NUM_PARAMS(value));
            assert(IS_END(typeset));

            Init_Block(D_OUT, copy);
            return R_OUT;
        }

        // We use a heuristic that if the first element of a function's body
        // is a series with the file and line bits set, then that's what it
        // returns for FILE OF and LINE OF.
        //
        case SYM_FILE: {
            if (NOT(ANY_SERIES(VAL_FUNC_BODY(value))))
                return R_BLANK;

            REBSER *s = VAL_SERIES(VAL_FUNC_BODY(value));

            if (NOT_SER_FLAG(s, SERIES_FLAG_FILE_LINE))
                return R_BLANK;

            // !!! How to tell whether it's a URL! or a FILE! ?
            //
            Scan_File(D_OUT, STR_HEAD(LINK(s).file), SER_LEN(LINK(s).file));
            return R_OUT; }

        case SYM_LINE: {
            if (NOT(ANY_SERIES(VAL_FUNC_BODY(value))))
                return R_BLANK;

            REBSER *s = VAL_SERIES(VAL_FUNC_BODY(value));

            if (NOT_SER_FLAG(s, SERIES_FLAG_FILE_LINE))
                return R_BLANK;

            Init_Integer(D_OUT, MISC(s).line);
            return R_OUT; }

        default:
            fail (Error_Cannot_Reflect(VAL_TYPE(value), arg));
        }
        break; }

    default:
        break;
    }

    fail (Error_Illegal_Action(VAL_TYPE(value), action));
}


//
//  func-class-of: native [
//
//  {Internal-use-only for implementing NATIVE?, ACTION?, CALLBACK?, etc.}
//
//      func [function!]
//  ]
//
REBNATIVE(func_class_of)
//
// !!! This is a stopgap measure.  Generally speaking, functions should be a
// "black box" to user code, and it's only in META-OF data that a function
// would choose to expose whether it is something like a specialization or an
// adaptation.
//
// Currently, BODY-OF relies on this.  But not only do not all functions have
// "bodies" (specializations, etc.) some have C code bodies (natives).
// With a variety of dispatchers, there would need to be some reverse lookup
// by dispatcher to reliably provide reflectors (META-OF could work but could
// get out of sync with the dispatcher, e.g. with hijacking)
{
    INCLUDE_PARAMS_OF_FUNC_CLASS_OF;

    REBVAL *value = ARG(func);
    REBCNT n;

    if (IS_FUNCTION_INTERPRETED(value))
        n = 2;
    else if (IS_FUNCTION_ACTION(value))
        n = 3;
    else if (IS_FUNCTION_SPECIALIZER(value))
        n = 7;
    else {
        // !!! A shaky guess, but assume native if none of the above.
        // (COMMAND! was once 4, 5 and 6 were routine and callback).
        n = 1;
    }

    Init_Integer(D_OUT, n);
    return R_OUT;
}


//
//  PD_Function: C
//
REB_R PD_Function(REBPVS *pvs, const REBVAL *picker, const REBVAL *opt_setval)
{
    UNUSED(pvs);
    UNUSED(opt_setval);

    if (IS_BLANK(picker)) {
        //
        // Leave the function value as-is, and continue processing.  This
        // enables things like `append/(all [foo 'dup])/only`...
        //
        return R_OUT;
    }

    // The first evaluation of a GROUP! and GET-WORD! are processed by the
    // general path mechanic before reaching this dispatch.  So if it's not
    // a word or one of those that evaluated to a word raise an error.
    //
    if (!IS_WORD(picker))
        fail (Error_Bad_Refine_Raw(picker));

    // We could generate a "refined" function variant at each step:
    //
    //     `append/dup/only` => `ad: :append/dup | ado: :ad/only | ado`
    //
    // Generating these intermediates would be costly.  They'd have updated
    // paramlists and tax the garbage collector.  So path dispatch is
    // understood to push the canonized word to the data stack in the
    // function case.
    //
    DS_PUSH(picker);

    // Go ahead and canonize the word symbol so we don't have to do it each
    // time in order to get a case-insensitive compare.  (Note that canons can
    // be GC'd, but will not be so long as an instance is on the stack.)
    //
    Canonize_Any_Word(DS_TOP);

    // Leave the function value as is in pvs->out
    //
    return R_OUT;
}
