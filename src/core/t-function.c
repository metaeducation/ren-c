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

static REBOOL Same_Action(const RELVAL *a1, const RELVAL *a2)
{
    assert(IS_ACTION(a1) && IS_ACTION(a2));

    if (VAL_ACT_PARAMLIST(a1) == VAL_ACT_PARAMLIST(a2)) {
        assert(VAL_ACT_DISPATCHER(a1) == VAL_ACT_DISPATCHER(a2));
        assert(VAL_ACT_BODY(a1) == VAL_ACT_BODY(a2));

        // All actions that have the same paramlist are not necessarily the
        // "same action".  For instance, every RETURN shares a common
        // paramlist, but the binding is different in the REBVAL instances
        // in order to know where to "exit from".

        return VAL_BINDING(a1) == VAL_BINDING(a2);
    }

    return FALSE;
}


//
//  CT_Action: C
//
REBINT CT_Action(const RELVAL *a1, const RELVAL *a2, REBINT mode)
{
    if (mode >= 0)
        return Same_Action(a1, a2) ? 1 : 0;
    return -1;
}


//
//  MAKE_Action: C
//
// For REB_ACTION and "make spec", there is a function spec block and then
// a block of Rebol code implementing that function.  In that case we expect
// that `def` should be:
//
//     [[spec] [body]]
//
void MAKE_Action(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    assert(kind == REB_ACTION);
    UNUSED(kind);

    if (
        not IS_BLOCK(arg)
        or VAL_LEN_AT(arg) != 2
        or not IS_BLOCK(VAL_ARRAY_AT(arg))
        or not IS_BLOCK(VAL_ARRAY_AT(arg) + 1)
    ){
        fail (Error_Bad_Make(REB_ACTION, arg));
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
    REBACT *act = Make_Interpreted_Action_May_Fail(
        spec,
        body,
        MKF_ANY_VALUE
    );

    Move_Value(out, ACT_ARCHETYPE(act));
}


//
//  TO_Action: C
//
// There is currently no meaning for TO ACTION!.  DOES will create an action
// from a BLOCK!, e.g. `x: does [1 + y]`, so TO ACTION! of a block doesn't
// need to do that (for instance).
//
void TO_Action(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    assert(kind == REB_ACTION);
    UNUSED(kind);

    UNUSED(out);

    fail (Error_Invalid(arg));
}


//
//  MF_Action: C
//
void MF_Action(REB_MOLD *mo, const RELVAL *v, REBOOL form)
{
    UNUSED(form);

    Pre_Mold(mo, v);

    Append_Utf8_Codepoint(mo->series, '[');

    // !!! The system is no longer keeping the spec of functions, in order
    // to focus on a generalized "meta info object" service.  MOLD of
    // functions temporarily uses the word list as a substitute (which
    // drops types)
    //
    REBARR *words_list = List_Func_Words(v, TRUE); // show pure locals
    Mold_Array_At(mo, words_list, 0, 0);
    Free_Array(words_list);

    // !!! Previously, ACTION! would mold the body out.  This created a large
    // amount of output, and also many function variations do not have
    // ordinary "bodies".  Review if Get_Maybe_Fake_Action_Body() should be
    // used for this case.
    //
    Append_Unencoded(mo->series, " [...]");

    Append_Utf8_Codepoint(mo->series, ']');
    End_Mold(mo);
}


//
//  REBTYPE: C
//
REBTYPE(Action)
{
    REBVAL *value = D_ARG(1);
    REBVAL *arg = D_ARGC > 1 ? D_ARG(2) : NULL;

    switch (verb) {
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
            VAL_ACT_PARAMLIST(value),
            SPECIFIED // !!! Note: not actually "deep", just typesets
        );
        ARR_HEAD(proxy_paramlist)->payload.action.paramlist
            = proxy_paramlist;
        MISC(proxy_paramlist).meta = VAL_ACT_META(value);
        SET_SER_FLAG(proxy_paramlist, ARRAY_FLAG_PARAMLIST);

        // If the function had code, then that code will be bound relative
        // to the original paramlist that's getting hijacked.  So when the
        // proxy is called, we want the frame pushed to be relative to
        // whatever underlied the function...even if it was foundational
        // so `underlying = VAL_ACTION(value)`

        REBACT *proxy = Make_Action(
            proxy_paramlist,
            ACT_DISPATCHER(VAL_ACTION(value)),
            ACT_FACADE(VAL_ACTION(value)), // can reuse the facade
            ACT_EXEMPLAR(VAL_ACTION(value)) // not changing the specialization
        );

        // A new body_holder was created inside Make_Action().  Rare case
        // where we can bit-copy a possibly-relative value.
        //
        Blit_Cell(ACT_BODY(proxy), VAL_ACT_BODY(value));

        Move_Value(D_OUT, ACT_ARCHETYPE(proxy));
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
            Get_Maybe_Fake_Action_Body(D_OUT, value);
            return R_OUT;

        case SYM_TYPES: {
            REBARR *copy = Make_Array(VAL_ACT_NUM_PARAMS(value));

            // The typesets have a symbol in them for the parameters, and
            // ordinary typesets aren't supposed to have it--that's a
            // special feature for object keys and paramlists!  So clear
            // that symbol out before giving it back.
            //
            REBVAL *param = VAL_ACT_PARAMS_HEAD(value);
            REBVAL *typeset = SINK(ARR_HEAD(copy));
            for (; NOT_END(param); param++, typeset++) {
                assert(VAL_PARAM_SPELLING(param) != NULL);
                Move_Value(typeset, param);
                INIT_TYPESET_NAME(typeset, NULL);
            }
            TERM_ARRAY_LEN(copy, VAL_ACT_NUM_PARAMS(value));
            assert(IS_END(typeset));

            Init_Block(D_OUT, copy);
            return R_OUT;
        }

        // We use a heuristic that if the first element of a function's body
        // is a series with the file and line bits set, then that's what it
        // returns for FILE OF and LINE OF.
        //
        case SYM_FILE: {
            if (not ANY_SERIES(VAL_ACT_BODY(value)))
                return R_BLANK;

            REBSER *s = VAL_SERIES(VAL_ACT_BODY(value));

            if (NOT_SER_FLAG(s, ARRAY_FLAG_FILE_LINE))
                return R_BLANK;

            // !!! How to tell whether it's a URL! or a FILE! ?
            //
            Scan_File(
                D_OUT, cb_cast(STR_HEAD(LINK(s).file)), SER_LEN(LINK(s).file)
            );
            return R_OUT; }

        case SYM_LINE: {
            if (not ANY_SERIES(VAL_ACT_BODY(value)))
                return R_BLANK;

            REBSER *s = VAL_SERIES(VAL_ACT_BODY(value));

            if (NOT_SER_FLAG(s, ARRAY_FLAG_FILE_LINE))
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

    fail (Error_Illegal_Action(VAL_TYPE(value), verb));
}


//
//  PD_Action: C
//
// We *could* generate a partially specialized action variant at each step:
//
//     `append/dup/only` => `ad: :append/dup | ado: :ad/only | ado`
//
// But generating these intermediates would be quite costly.  So what is done
// instead is each step pushes a canonized word to the stack.  The processing
// for GET-PATH! will--at the end--make a partially refined ACTION! value
// (see WORD_FLAG_PARTIAL_REFINE).  But the processing for REB_PATH in
// Do_Core() does not need to...it operates off of the stack values directly.
//
REB_R PD_Action(REBPVS *pvs, const REBVAL *picker, const REBVAL *opt_setval)
{
    UNUSED(opt_setval);

    assert(IS_ACTION(pvs->out));
    UNUSED(pvs);

    if (IS_BLANK(picker)) {
        //
        // Leave the function value as-is, and continue processing.  This
        // enables things like `append/(all [only 'only])/dup`...
        //
        // Note this feature doesn't have obvious applications to refinements
        // that take arguments...only ones that don't.  Use "revoking" to
        // pass void as arguments to a refinement that is always present
        // in that case.
        //
        // Void might seem more convenient, e.g. `append/(only ?? 'only)/dup`,
        // however it is disallowed to use voids at the higher level path
        // protocol.  This is probably for the best.
        //
        return R_OUT;
    }

    // The first evaluation of a GROUP! and GET-WORD! are processed by the
    // general path mechanic before reaching this dispatch.  So if it's not
    // a word or one of those that evaluated to a word raise an error.
    //
    if (!IS_WORD(picker))
        fail (Error_Bad_Refine_Raw(picker));

    DS_PUSH_TRASH;
    Init_Refinement(DS_TOP, VAL_WORD_CANON(picker)); // canonize just once

    // Leave the function value as is in pvs->out
    //
    return R_OUT;
}
