//
//  file: %t-function.c
//  summary: "function related datatypes"
//  section: datatypes
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
    assert(Is_Action(a1) && Is_Action(a2));

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
// MAKE ACTION! is replaced by LAMBDA and FUNC(TION).
// FUNCTION is a synonym for FUNC in in the main branch.
//
Bounce MAKE_Action(Value* out, enum Reb_Kind kind, const Value* arg)
{
    assert(kind == TYPE_ACTION);
    UNUSED(kind);
    UNUSED(out);

    panic (Error_Bad_Make(TYPE_ACTION, arg));
}


//
//  TO_Action: C
//
// There is currently no meaning for TO ACTION!.  DOES will create an action
// from a BLOCK!, e.g. `x: does [1 + y]`, so TO ACTION! of a block doesn't
// need to do that (for instance).
//
Bounce TO_Action(Value* out, enum Reb_Kind kind, const Value* arg)
{
    assert(kind == TYPE_ACTION);
    UNUSED(kind);

    UNUSED(out);

    panic (Error_Invalid(arg));
}


//
//  MF_Action: C
//
void MF_Action(Molder* mo, const Cell* v, bool form)
{
    UNUSED(form);

    Begin_Non_Lexical_Mold(mo, v);

    Append_Codepoint(mo->utf8flex, '[');

    // !!! The system is no longer keeping the spec of functions, in order
    // to focus on a generalized "meta info object" service.  MOLD of
    // functions temporarily uses the word list as a substitute (which
    // drops types)
    //
    Array* words_list = List_Func_Words(v, true); // show pure locals
    Mold_Array_At(mo, words_list, 0, "[]");
    Free_Unmanaged_Flex(words_list);

    // !!! Previously, ACTION! would mold the body out.  This created a large
    // amount of output, and also many function variations do not have
    // ordinary "bodies".  Review if Get_Maybe_Fake_Action_Body() should be
    // used for this case.
    //
    Append_Unencoded(mo->utf8flex, " [...]");

    Append_Codepoint(mo->utf8flex, ']');
    End_Non_Lexical_Mold(mo);
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

        UNUSED(PARAM(VALUE));
        if (Bool_ARG(PART)) {
            UNUSED(ARG(LIMIT));
            panic (Error_Bad_Refines_Raw());
        }
        if (Bool_ARG(TYPES)) {
            UNUSED(ARG(KINDS));
            panic (Error_Bad_Refines_Raw());
        }
        if (Bool_ARG(DEEP)) {
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
        Array_Head(proxy_paramlist)->payload.action.paramlist
            = proxy_paramlist;
        MISC(proxy_paramlist).meta = ACT_META(act);

        // If the function had code, then that code will be bound relative
        // to the original paramlist that's getting hijacked.  So when the
        // proxy is called, we want the frame pushed to be relative to
        // whatever underlied the function...even if it was foundational
        // so `underlying = VAL_ACTION(value)`

        REBLEN details_len = Array_Len(ACT_DETAILS(act));
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
        Cell* src = Array_Head(ACT_DETAILS(act));
        Cell* dest = Array_Head(ACT_DETAILS(proxy));
        for (; NOT_END(src); ++src, ++dest)
            Blit_Cell(dest, src);
        Term_Array_Len(ACT_DETAILS(proxy), details_len);

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
            Array* copy = Make_Array(VAL_ACT_NUM_PARAMS(value));

            // The typesets have a symbol in them for the parameters, and
            // ordinary typesets aren't supposed to have it--that's a
            // special feature for object keys and paramlists!  So clear
            // that symbol out before giving it back.
            //
            Value* param = VAL_ACT_PARAMS_HEAD(value);
            Value* typeset = KNOWN(Array_Head(copy));
            for (; NOT_END(param); param++, typeset++) {
                assert(Cell_Parameter_Symbol(param) != nullptr);
                Copy_Cell(typeset, param);
                INIT_TYPESET_NAME(typeset, nullptr);
            }
            Term_Array_Len(copy, VAL_ACT_NUM_PARAMS(value));
            assert(IS_END(typeset));

            return Init_Block(OUT, copy);
        }

        // We use a heuristic that if the first element of a function's body
        // is a series with the file and line bits set, then that's what it
        // returns for FILE OF and LINE OF.
        //
        case SYM_FILE: {
            Array* details = VAL_ACT_DETAILS(value);
            if (Array_Len(details) < 1)
                return nullptr;

            if (not Any_List(Array_Head(details)))
                return nullptr;

            Array* a = Cell_Array(Array_Head(details));
            if (Not_Array_Flag(a, HAS_FILE_LINE))
                return nullptr;

            // !!! How to tell whether it's a URL! or a FILE! ?
            //
            Init_File(OUT, LINK(a).file);
            return OUT; }

        case SYM_LINE: {
            Array* details = VAL_ACT_DETAILS(value);
            if (Array_Len(details) < 1)
                return nullptr;

            if (not Any_List(Array_Head(details)))
                return nullptr;

            Array* a = Cell_Array(Array_Head(details));
            if (Not_Array_Flag(a, HAS_FILE_LINE))
                return nullptr;

            return Init_Integer(OUT, MISC(a).line); }

        default:
            panic (Error_Cannot_Reflect(Type_Of(value), arg));
        }
        break; }

    default:
        break;
    }

    panic (Error_Illegal_Action(Type_Of(value), verb));
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
// for GET-PATH! will--at the end--make a partially refined ACTION! value.
//
Bounce PD_Action(
    REBPVS *pvs,
    const Value* picker,
    const Value* opt_setval
){
    UNUSED(opt_setval);

    assert(Is_Action(pvs->out));

    if (Is_Void(picker)) {
        //
        // Leave the function value as-is, and continue processing.  This
        // enables things like `append/(if only [/only])/dup`...
        //
        // Note this feature doesn't have obvious applications to refinements
        // that take arguments...only ones that don't.  Use "revoking" to
        // pass void as arguments to a refinement that is always present
        // in that case.
        //
        return pvs->out;
    }

    // The first evaluation of a GROUP! and GET-WORD! are processed by the
    // general path mechanic before reaching this dispatch.  So if it's not
    // a word/refinement or or one of those that evaluated it, then error.
    //
    if (not Is_Word(picker) and not Is_Refinement(picker))
        panic (Error_Bad_Refine_Raw(picker));

    Init_Issue(PUSH(), VAL_WORD_CANON(picker));  // canonize just once

    // Leave the function value as is in pvs->out
    //
    return pvs->out;
}
