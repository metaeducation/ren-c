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
//=////////////////////////////////////////////////////////////////////////=//
//

#include "sys-core.h"

static bool Same_Action(const REBCEL *a, const REBCEL *b)
{
    assert(CELL_KIND(a) == REB_ACTION and CELL_KIND(b) == REB_ACTION);

    if (VAL_ACT_PARAMLIST(a) == VAL_ACT_PARAMLIST(b)) {
        assert(VAL_ACT_DETAILS(a) == VAL_ACT_DETAILS(b));

        // All actions that have the same paramlist are not necessarily the
        // "same action".  For instance, every RETURN shares a common
        // paramlist, but the binding is different in the REBVAL instances
        // in order to know where to "exit from".
        //
        return VAL_BINDING(a) == VAL_BINDING(b);
    }

    return false;
}


//
//  CT_Action: C
//
REBINT CT_Action(const REBCEL *a, const REBCEL *b, REBINT mode)
{
    if (mode >= 0)
        return Same_Action(a, b) ? 1 : 0;
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
// !!! This has a potential to redesign as a single block, see concept:
//
// https://forum.rebol.info/t/1002
//
REB_R MAKE_Action(
    REBVAL *out,
    enum Reb_Kind kind,
    const REBVAL *opt_parent,
    const REBVAL *arg
){
    assert(kind == REB_ACTION);
    if (opt_parent)
        fail (Error_Bad_Make_Parent(kind, opt_parent));

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

    return Init_Action_Unbound(out, act);
}


//
//  TO_Action: C
//
// There is currently no meaning for TO ACTION!.  DOES will create an action
// from a BLOCK!, e.g. `x: does [1 + y]`, so TO ACTION! of a block doesn't
// need to do that (for instance).
//
REB_R TO_Action(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    assert(kind == REB_ACTION);
    UNUSED(kind);

    UNUSED(out);

    fail (arg);
}


//
//  MF_Action: C
//
void MF_Action(REB_MOLD *mo, const REBCEL *v, bool form)
{
    UNUSED(form);

    Pre_Mold(mo, v);

    Append_Codepoint(mo->series, '[');

    // !!! The system is no longer keeping the spec of functions, in order
    // to focus on a generalized "meta info object" service.  MOLD of
    // functions temporarily uses the word list as a substitute (which
    // drops types)
    //
    REBARR *parameters = Make_Action_Parameters_Arr(VAL_ACTION(v));
    Mold_Array_At(mo, parameters, 0, "[]");
    Free_Unmanaged_Array(parameters);

    // !!! Previously, ACTION! would mold the body out.  This created a large
    // amount of output, and also many function variations do not have
    // ordinary "bodies".  Review if Get_Maybe_Fake_Action_Body() should be
    // used for this case.
    //
    Append_Ascii(mo->series, " [...]");

    Append_Codepoint(mo->series, ']');
    End_Mold(mo);
}


//
//  REBTYPE: C
//
REBTYPE(Action)
{
    REBVAL *value = D_ARG(1);
    REBVAL *arg = D_ARGC > 1 ? D_ARG(2) : NULL;

    switch (VAL_WORD_SYM(verb)) {
    case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;

        UNUSED(PAR(value));

        if (REF(part) or REF(types))
            fail (Error_Bad_Refines_Raw());

        if (REF(deep)) {
            // !!! always "deep", allow it?
        }

        REBACT *act = VAL_ACTION(value);

        // Copying functions creates another handle which executes the same
        // code, yet has a distinct identity.  This means it would not be
        // HIJACK'd if the function that it was copied from was.

        REBARR *proxy_paramlist = Copy_Array_Deep_Flags_Managed(
            ACT_PARAMLIST(act),
            SPECIFIED,  // !!! Note: not actually "deep", just typesets
            SERIES_MASK_PARAMLIST
        );
        VAL_ACT_PARAMLIST_NODE(ARR_HEAD(proxy_paramlist))
            = NOD(proxy_paramlist);
        MISC_META_NODE(proxy_paramlist) = NOD(ACT_META(act));

        // If the function had code, then that code will be bound relative
        // to the original paramlist that's getting hijacked.  So when the
        // proxy is called, we want the frame pushed to be relative to
        // whatever underlied the function...even if it was foundational
        // so `underlying = VAL_ACTION(value)`

        REBCNT details_len = ARR_LEN(ACT_DETAILS(act));
        REBACT *proxy = Make_Action(
            proxy_paramlist,
            ACT_DISPATCHER(act),
            ACT_UNDERLYING(act),  // !!! ^-- see notes above RE: frame pushing
            ACT_EXEMPLAR(act),  // not changing the specialization
            details_len  // details array capacity
        );

        // A new body_holder was created inside Make_Action().  Rare case
        // where we can bit-copy a possibly-relative value.
        //
        RELVAL *src = ARR_HEAD(ACT_DETAILS(act));
        RELVAL *dest = ARR_HEAD(ACT_DETAILS(proxy));
        for (; NOT_END(src); ++src, ++dest)
            Blit_Cell(dest, src);
        TERM_ARRAY_LEN(ACT_DETAILS(proxy), details_len);

        return Init_Action_Maybe_Bound(D_OUT, proxy, VAL_BINDING(value)); }

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value));

        REBSYM property = VAL_WORD_SYM(ARG(property));
        switch (property) {
          case SYM_BINDING: {
            if (Did_Get_Binding_Of(D_OUT, value))
                return D_OUT;
            return nullptr; }

          case SYM_WORDS:
            fail ("Use PARAMETERS OF for parameter ordering, not WORDS OF");

          case SYM_PARAMETERS:
            return Init_Block(
                D_OUT,
                Make_Action_Parameters_Arr(VAL_ACTION(value))
            );

          case SYM_TYPESETS:
            return Init_Block(
                D_OUT,
                Make_Action_Typesets_Arr(VAL_ACTION(value))
            );

          case SYM_BODY:
            Get_Maybe_Fake_Action_Body(D_OUT, value);
            return D_OUT;

          case SYM_TYPES: {
            REBARR *copy = Make_Array(VAL_ACT_NUM_PARAMS(value));

            // The typesets have a symbol in them for the parameters, and
            // ordinary typesets aren't supposed to have it--that's a
            // special feature for object keys and paramlists!  So clear
            // that symbol out before giving it back.
            //
            REBVAL *param = VAL_ACT_PARAMS_HEAD(value);
            REBVAL *typeset = KNOWN(ARR_HEAD(copy));
            for (; NOT_END(param); ++param, ++typeset) {
                assert(IS_PARAM(param));
                RESET_CELL(typeset, REB_TYPESET, CELL_MASK_NONE);
                VAL_TYPESET_LOW_BITS(typeset) = VAL_TYPESET_LOW_BITS(param);
                VAL_TYPESET_HIGH_BITS(typeset) = VAL_TYPESET_HIGH_BITS(param);
            }
            TERM_ARRAY_LEN(copy, VAL_ACT_NUM_PARAMS(value));
            assert(IS_END(typeset));

            return Init_Block(D_OUT, copy); }

          case SYM_FILE:
          case SYM_LINE: {
            //
            // Use a heuristic that if the first element of a function's body
            // is a series with the file and line bits set, then that's what
            // it returns for FILE OF and LINE OF.

            REBARR *details = VAL_ACT_DETAILS(value);
            if (ARR_LEN(details) < 1 or not ANY_ARRAY(ARR_HEAD(details)))
                return nullptr;

            REBARR *a = VAL_ARRAY(ARR_HEAD(details));
            if (NOT_ARRAY_FLAG(a, HAS_FILE_LINE_UNMASKED))
                return nullptr;

            if (property == SYM_FILE)  // !!! How to tell URL! vs FILE! ?
                Init_File(D_OUT, LINK_FILE(a));
            else
                Init_Integer(D_OUT, MISC(a).line);

            return D_OUT; }

          default:
            fail (Error_Cannot_Reflect(VAL_TYPE(value), arg));
        }
        break; }

      default:
        break;
    }

    return R_UNHANDLED;
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
// Eval_Core() does not need to...it operates off stack values directly.
//
REB_R PD_Action(
    REBPVS *pvs,
    const REBVAL *picker,
    const REBVAL *opt_setval
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
    REBSTR *spelling;
    if (IS_WORD(picker))
        spelling = VAL_WORD_SPELLING(picker);
    else if (IS_REFINEMENT(picker))
        spelling = VAL_REFINEMENT_SPELLING(picker);
    else
        fail (Error_Bad_Refine_Raw(picker));

    Init_Issue(DS_PUSH(), STR_CANON(spelling)); // canonize just once

    return pvs->out; // leave ACTION! value in pvs->out, as-is
}
