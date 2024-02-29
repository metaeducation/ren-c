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

static bool Same_Action(const Cell* a1, const Cell* a2)
{
    assert(IS_ACTION(a1) && IS_ACTION(a2));

    if (VAL_ACT_PARAMLIST(a1) == VAL_ACT_PARAMLIST(a2)) {
        assert(VAL_ACT_DETAILS(a1) == VAL_ACT_DETAILS(a2));

        // All actions that have the same paramlist are not necessarily the
        // "same action".  For instance, every RETURN shares a common
        // paramlist, but the binding is different in the cell instances
        // in order to know where to "exit from".

        return VAL_BINDING(a1) == VAL_BINDING(a2);
    }

    return false;
}


//
//  CT_Action: C
//
REBINT CT_Action(const Cell* a1, const Cell* a2, REBINT mode)
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
REB_R MAKE_Action(Value* out, enum Reb_Kind kind, const Value* arg)
{
    assert(kind == REB_ACTION);
    UNUSED(kind);

    if (
        not IS_BLOCK(arg)
        or VAL_LEN_AT(arg) != 2
        or not IS_BLOCK(Cell_Array_At(arg))
        or not IS_BLOCK(Cell_Array_At(arg) + 1)
    ){
        fail (Error_Bad_Make(REB_ACTION, arg));
    }

    DECLARE_VALUE (spec);
    Derelativize(spec, Cell_Array_At(arg), VAL_SPECIFIER(arg));

    DECLARE_VALUE (body);
    Derelativize(body, Cell_Array_At(arg) + 1, VAL_SPECIFIER(arg));

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

    return Init_Action_Unbound(out, act);
}


//
//  TO_Action: C
//
// There is currently no meaning for TO ACTION!.  DOES will create an action
// from a BLOCK!, e.g. `x: does [1 + y]`, so TO ACTION! of a block doesn't
// need to do that (for instance).
//
REB_R TO_Action(Value* out, enum Reb_Kind kind, const Value* arg)
{
    assert(kind == REB_ACTION);
    UNUSED(kind);

    UNUSED(out);

    fail (Error_Invalid(arg));
}


//
//  MF_Action: C
//
void MF_Action(REB_MOLD *mo, const Cell* v, bool form)
{
    UNUSED(form);

    Pre_Mold(mo, v);

    Append_Utf8_Codepoint(mo->series, '[');

    // !!! The system is no longer keeping the spec of functions, in order
    // to focus on a generalized "meta info object" service.  MOLD of
    // functions temporarily uses the word list as a substitute (which
    // drops types)
    //
    Array* words_list = List_Func_Words(v, true); // show pure locals
    Mold_Array_At(mo, words_list, 0, "[]");
    Free_Unmanaged_Array(words_list);

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
    Value* value = D_ARG(1);
    Value* arg = D_ARGC > 1 ? D_ARG(2) : nullptr;

    switch (Cell_Word_Id(verb)) {
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

        REBACT *act = VAL_ACTION(value);

        // Copying functions creates another handle which executes the same
        // code, yet has a distinct identity.  This means it would not be
        // HIJACK'd if the function that it was copied from was.

        Array* proxy_paramlist = Copy_Array_Deep_Flags_Managed(
            ACT_PARAMLIST(act),
            SPECIFIED, // !!! Note: not actually "deep", just typesets
            SERIES_MASK_ACTION
        );
        ARR_HEAD(proxy_paramlist)->payload.action.paramlist
            = proxy_paramlist;
        MISC(proxy_paramlist).meta = ACT_META(act);

        // If the function had code, then that code will be bound relative
        // to the original paramlist that's getting hijacked.  So when the
        // proxy is called, we want the frame pushed to be relative to
        // whatever underlied the function...even if it was foundational
        // so `underlying = VAL_ACTION(value)`

        REBLEN details_len = ARR_LEN(ACT_DETAILS(act));
        REBACT *proxy = Make_Action(
            proxy_paramlist,
            ACT_DISPATCHER(act),
            ACT_UNDERLYING(act), // !!! ^-- see notes above RE: frame pushing
            ACT_EXEMPLAR(act), // not changing the specialization
            details_len // details array capacity
        );

        // A new body_holder was created inside Make_Action().  Rare case
        // where we can bit-copy a possibly-relative value.
        //
        Cell* src = ARR_HEAD(ACT_DETAILS(act));
        Cell* dest = ARR_HEAD(ACT_DETAILS(proxy));
        for (; NOT_END(src); ++src, ++dest)
            Blit_Cell(dest, src);
        TERM_ARRAY_LEN(ACT_DETAILS(proxy), details_len);

        return Init_Action_Maybe_Bound(OUT, proxy, VAL_BINDING(value)); }

    case SYM_REFLECT: {
        Option(SymId) sym = Cell_Word_Id(arg);

        switch (sym) {

        case SYM_BINDING: {
            if (Did_Get_Binding_Of(OUT, value))
                return OUT;
            return nullptr; }

        case SYM_WORDS:
            Init_Block(OUT, List_Func_Words(value, false)); // no locals
            return OUT;

        case SYM_BODY:
            Get_Maybe_Fake_Action_Body(OUT, value);
            return OUT;

        case SYM_TYPES: {
            Array* copy = Make_Arr(VAL_ACT_NUM_PARAMS(value));

            // The typesets have a symbol in them for the parameters, and
            // ordinary typesets aren't supposed to have it--that's a
            // special feature for object keys and paramlists!  So clear
            // that symbol out before giving it back.
            //
            Value* param = VAL_ACT_PARAMS_HEAD(value);
            Value* typeset = KNOWN(ARR_HEAD(copy));
            for (; NOT_END(param); param++, typeset++) {
                assert(Cell_Parameter_Symbol(param) != nullptr);
                Move_Value(typeset, param);
                INIT_TYPESET_NAME(typeset, nullptr);
            }
            TERM_ARRAY_LEN(copy, VAL_ACT_NUM_PARAMS(value));
            assert(IS_END(typeset));

            return Init_Block(OUT, copy);
        }

        // We use a heuristic that if the first element of a function's body
        // is a series with the file and line bits set, then that's what it
        // returns for FILE OF and LINE OF.
        //
        case SYM_FILE: {
            Array* details = VAL_ACT_DETAILS(value);
            if (ARR_LEN(details) < 1)
                return nullptr;

            if (not ANY_ARRAY(ARR_HEAD(details)))
                return nullptr;

            Array* a = Cell_Array(ARR_HEAD(details));
            if (NOT_SER_FLAG(a, ARRAY_FLAG_FILE_LINE))
                return nullptr;

            // !!! How to tell whether it's a URL! or a FILE! ?
            //
            Scan_File(
                OUT, cb_cast(Symbol_Head(LINK(a).file)), SER_LEN(LINK(a).file)
            );
            return OUT; }

        case SYM_LINE: {
            Array* details = VAL_ACT_DETAILS(value);
            if (ARR_LEN(details) < 1)
                return nullptr;

            if (not ANY_ARRAY(ARR_HEAD(details)))
                return nullptr;

            Array* a = Cell_Array(ARR_HEAD(details));
            if (NOT_SER_FLAG(a, ARRAY_FLAG_FILE_LINE))
                return nullptr;

            return Init_Integer(OUT, MISC(a).line); }

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
// Eval_Core_Throws() does not need to...it operates off stack values directly.
//
REB_R PD_Action(
    REBPVS *pvs,
    const Value* picker,
    const Value* opt_setval
){
    UNUSED(opt_setval);

    assert(IS_ACTION(pvs->out));

    if (IS_BLANK(picker)) {
        //
        // Leave the function value as-is, and continue processing.  This
        // enables things like `append/(either only [/only] [_])/dup`...
        //
        // Note this feature doesn't have obvious applications to refinements
        // that take arguments...only ones that don't.  Use "revoking" to
        // pass void as arguments to a refinement that is always present
        // in that case.
        //
        // Null might seem more convenient, for `append/(if only [/only])/dup`
        // however it is disallowed to use nulls at the higher level path
        // protocol.  This is probably for the best.
        //
        return pvs->out;
    }

    // The first evaluation of a GROUP! and GET-WORD! are processed by the
    // general path mechanic before reaching this dispatch.  So if it's not
    // a word/refinement or or one of those that evaluated it, then error.
    //
    if (not IS_WORD(picker) and not IS_REFINEMENT(picker))
        fail (Error_Bad_Refine_Raw(picker));

    DS_PUSH_TRASH;
    Init_Issue(DS_TOP, VAL_WORD_CANON(picker)); // canonize just once

    // Leave the function value as is in pvs->out
    //
    return pvs->out;
}
