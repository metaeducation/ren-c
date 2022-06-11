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
//  Copied_Dispatcher: C
//
// Update action identity that was pushed.
//
REB_R Copied_Dispatcher(REBFRM *f)
{
    REBVAL *archetype = ACT_ARCHETYPE(FRM_PHASE(f));

    //REBCTX *exemplar = ACT_EXEMPLAR(FRM_PHASE(f));

    INIT_FRM_PHASE(f, VAL_ACTION(archetype));
    //INIT_FRM_BINDING(f, CTX_FRAME_BINDING(exemplar));

    // !!! Is it necessary to call REDO or could we just go ahead and call
    // the dispatcher ourself?

    return R_REDO_UNCHECKED; // redo uses the updated phase and binding
}



static bool Same_Action(noquote(const Cell*) a, noquote(const Cell*) b)
{
    assert(CELL_HEART(a) == REB_ACTION and CELL_HEART(b) == REB_ACTION);

    if (VAL_ACTION_KEYLIST(a) == VAL_ACTION_KEYLIST(b)) {
        //
        // All actions that have the same paramlist are not necessarily the
        // "same action".  For instance, every RETURN shares a common
        // paramlist, but the binding is different in the REBVAL instances
        // in order to know where to "exit from".
        //
        return VAL_ACTION_BINDING(a) == VAL_ACTION_BINDING(b);
    }

    return false;
}


//
//  CT_Action: C
//
REBINT CT_Action(noquote(const Cell*) a, noquote(const Cell*) b, bool strict)
{
    UNUSED(strict);  // no lax form of comparison

    if (Same_Action(a, b))
        return 0;
    assert(VAL_ACTION(a) != VAL_ACTION(b));
    return a > b ? 1 : -1;  // !!! Review arbitrary ordering
}


//
//  MAKE_Action: C
//
// Ren-C provides the ability to MAKE ACTION! from a FRAME!.  Any values on
// the public interface which are `~` isotopes aree assumed unspecialized.
//
// https://forum.rebol.info/t/default-values-and-make-frame/1412
//
// It however does not carry forward R3-Alpha's concept of MAKE ACTION! from
// a BLOCK!, e.g. `make function! copy/deep reduce [spec body]`.  This is
// because there is no particular advantage to folding the two parameters to
// FUNC into one block...and it makes spec analysis seem more "cooked in"
// than being an epicycle of the design of FUNC (which is just an optimized
// version of something that could be written in usermode).
//
REB_R MAKE_Action(
    REBVAL *out,
    enum Reb_Kind kind,
    option(const REBVAL*) parent,
    const REBVAL *arg
){
    assert(kind == REB_ACTION);
    if (parent)
        fail (Error_Bad_Make_Parent(kind, unwrap(parent)));

    if (IS_FRAME(arg)) {  // will assume `~` isotope fields are unspecialized
        //
        // !!! This makes a copy of the incoming context.  AS FRAME! does not,
        // but it expects any specialized frame fields to be hidden, and non
        // hidden fields are parameter specifications.  Review if there is
        // some middle ground.
        //
        REBVAL *frame_copy = rebValue("copy", arg);
        REBCTX *exemplar = VAL_CONTEXT(frame_copy);
        rebRelease(frame_copy);

        return Init_Action(
            out,
            Make_Action_From_Exemplar(exemplar),
            VAL_FRAME_LABEL(arg),
            VAL_FRAME_BINDING(arg)
        );
    }

    if (not IS_BLOCK(arg))
        fail (Error_Bad_Make(REB_ACTION, arg));

    fail ("Ren-C does not support MAKE ACTION! on BLOCK! (see FUNC*/FUNC)");
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
void MF_Action(REB_MOLD *mo, noquote(const Cell*) v, bool form)
{
    UNUSED(form);

    Append_Ascii(mo->series, "#[action! ");

    option(const REBSTR*) label = VAL_ACTION_LABEL(v);
    if (label) {
        Append_Codepoint(mo->series, '{');
        Append_Spelling(mo->series, unwrap(label));
        Append_Ascii(mo->series, "} ");
    }

    // !!! The system is no longer keeping the spec of functions, in order
    // to focus on a generalized "meta info object" service.  MOLD of
    // functions temporarily uses the word list as a substitute (which
    // drops types)
    //
    const bool just_words = false;
    REBARR *parameters = Make_Action_Parameters_Arr(VAL_ACTION(v), just_words);
    Mold_Array_At(mo, parameters, 0, "[]");
    Free_Unmanaged_Series(parameters);

    // !!! Previously, ACTION! would mold the body out.  This created a large
    // amount of output, and also many function variations do not have
    // ordinary "bodies".  It's more useful to show the cached name, and maybe
    // some base64 encoding of a UUID (?)  In the meantime, having the label
    // of the last word used is actually a lot more useful than most things.

    Append_Codepoint(mo->series, ']');
    End_Mold(mo);
}


//
//  REBTYPE: C
//
REBTYPE(Action)
{
    REBVAL *action = D_ARG(1);
    REBACT *act = VAL_ACTION(action);

    switch (ID_OF_SYMBOL(verb)) {

  //=//// PICK* (see %sys-pick.h for explanation) //////////////////////////=//

    // !!! This is an interim implementation hack for REDBOL-PATHS, which
    // transforms something like `lib/append/dup` into `lib.append.dup` when
    // it notices that LIB is not an ACTION!.  This is a *very slow* way of
    // dealing with refinements because it produces a specialized action
    // at each stage.

      case SYM_PICK_P: {
        INCLUDE_PARAMS_OF_PICK_P;
        UNUSED(ARG(location));

        REBVAL *redbol = Get_System(SYS_OPTIONS, OPTIONS_REDBOL_PATHS);
        if (not IS_LOGIC(redbol) or VAL_LOGIC(redbol) == false) {
            fail (
                "SYSTEM.OPTIONS.REDBOL-PATHS is false, so you can't"
                " use paths to do ordinary picking.  Use TUPLE!"
            );
          }

        REBVAL *picker = ARG(picker);
        if (Is_Null_Isotope(picker) or IS_BLANK(picker))
            return_value (action);

        const REBSYM *symbol;
        if (IS_WORD(picker))
            symbol = VAL_WORD_SYMBOL(picker);
        else if (IS_PATH(picker) and IS_REFINEMENT(picker))
            symbol = VAL_REFINEMENT_SYMBOL(picker);
        else
            fail (picker);

        REBDSP dsp_orig = DSP;
        Init_Word(DS_PUSH(), symbol);
        if (Specialize_Action_Throws(OUT, action, nullptr, dsp_orig))
            return_thrown (OUT);

        return OUT; }

  //=//// COPY /////////////////////////////////////////////////////////////=//

    // Being able to COPY functions was added so that you could create a new
    // function identity which behaved the same as an existing function, but
    // kept working if the original function was HIJACK'ed.  (See %c-hijack.c)
    // To do this means being able to create an independent identity that can
    // run the same code without needing to invoke the prior identity to do so.
    //
    // (By contrast: specialization also creates a new identity, but then falls
    // through via a reference to the old identity to run the implementation.
    // Hence hijacking a function that has been specialized will hijack all of
    // its specializations.)
    //
    // Originally COPY was done just by copying the details array.  But that
    // puts two copies of the details array in play--which can be technically
    // dangerous, since the relationship between a function dispatcher and its
    // details is currently treated as a black box.  (The array could contain a
    // reference to an arbitrary C pointer, which might get freed in one clone
    // with an extant reference still lingering in the other.)
    //
    // The modified solution tweaks it so that the identity array for an
    // action is not necessarily where it looks for its ACT_DETAILS(), with
    // the details instead coming out of the archetype slot [0] of that array.
    //
    // !!! There are higher-level interesting mechanics that might be called
    // COPY that aren't covered at all here.  For instance: Someone might like
    // to have a generator that counts from 1 to 10 that is at 5, and be able
    // to COPY it...then have two generators that will count from 5 to 10
    // independently.  That requires methodization and cooperation with the
    // specific dispatcher.

      case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;

        UNUSED(PAR(value));

        if (REF(part) or REF(types))
            fail (Error_Bad_Refines_Raw());

        if (REF(deep)) {
            // !!! always "deep", allow it?
        }

        // If the function had code, then that code will be bound relative
        // to the original paramlist that's getting hijacked.  So when the
        // proxy is called, we want the frame pushed to be relative to
        // whatever underlied the function...even if it was foundational
        // so `underlying = VAL_ACTION(value)`

        REBACT *proxy = Make_Action(
            ACT_PARAMLIST(act),  // not changing the interface
            ACT_PARTIALS(act),  // keeping partial specializations
            ACT_DISPATCHER(act),  // have to preserve in case original hijacked
            //
            // While the copy doesn't need any details array of its own, it
            // has to be a dynamic allocation in order for ACT_DETAILS() to
            // assume the array is dynamic and beeline for the array.  We put
            // a dummy value ~copy~ in the array.  We assume this is better
            // than making ACT_DETAILS() have to check the dynamic series bit,
            // just because COPY on actions is so rare.
            2
        );

        REBARR *details = ACT_DETAILS(proxy);
        Init_Bad_Word(ARR_AT(details, 1), Canon(COPY));  // dummy ~copy~

        REBCTX *meta = ACT_META(act);
        assert(ACT_META(proxy) == nullptr);
        mutable_ACT_META(proxy) = meta;  // !!! Note: not a copy of meta

        if (GET_ACTION_FLAG(act, IS_NATIVE))
            SET_ACTION_FLAG(proxy, IS_NATIVE);

        Copy_Cell(ACT_ARCHETYPE(proxy), ACT_ARCHETYPE(act));

        return Init_Action(
            OUT,
            proxy,
            VAL_ACTION_LABEL(action),  // keep symbol (if any) from original
            VAL_ACTION_BINDING(action)  // same (e.g. RETURN to same frame)
        ); }

      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value));

        REBVAL *property = ARG(property);
        SYMID sym = VAL_WORD_ID(property);
        switch (sym) {
          case SYM_BINDING: {
            if (Did_Get_Binding_Of(OUT, action))
                return OUT;
            return nullptr; }

          case SYM_LABEL: {
            option(const REBSYM*) label = VAL_ACTION_LABEL(action);
            if (not label)
                return nullptr;
            return Init_Word(OUT, unwrap(label)); }

          case SYM_WORDS:
          case SYM_PARAMETERS: {
            bool just_words = (sym == SYM_WORDS);
            return Init_Block(
                OUT,
                Make_Action_Parameters_Arr(act, just_words)
            ); }

          case SYM_BODY:
            Get_Maybe_Fake_Action_Body(OUT, action);
            return OUT;

          case SYM_EXEMPLAR: {
            //
            // We give back the exemplar of the frame, which contains the
            // parameter descriptions.  Since exemplars are reused, this is
            // not enough to make the right action out of...so the phase has
            // to be set to the action that we are returning.
            //
            // !!! This loses the label information.  Technically the space
            // for the varlist could be reclaimed in this case and a label
            // used, as the read-only frame is archetypal.
            //
            Reset_Cell_Header_Untracked(
                TRACK(OUT), REB_FRAME, CELL_MASK_CONTEXT
            );
            INIT_VAL_CONTEXT_VARLIST(OUT, ACT_PARAMLIST(act));
            mutable_BINDING(OUT) = VAL_ACTION_BINDING(action);
            INIT_VAL_FRAME_PHASE_OR_LABEL(OUT, act);
            return OUT; }

          case SYM_TYPES:
            return Copy_Cell(OUT, CTX_ARCHETYPE(ACT_EXEMPLAR(act)));

          case SYM_FILE:
          case SYM_LINE: {
            //
            // Use a heuristic that if the first element of a function's body
            // is a series with the file and line bits set, then that's what
            // it returns for FILE OF and LINE OF.

            REBARR *details = ACT_DETAILS(act);
            if (ARR_LEN(details) < 1 or not ANY_ARRAY(ARR_HEAD(details)))
                return nullptr;

            const REBARR *a = VAL_ARRAY(ARR_HEAD(details));
            if (NOT_SUBCLASS_FLAG(ARRAY, a, HAS_FILE_LINE_UNMASKED))
                return nullptr;

            // !!! How to tell URL! vs FILE! ?
            //
            if (VAL_WORD_ID(property) == SYM_FILE)
                Init_File(OUT, LINK(Filename, a));
            else
                Init_Integer(OUT, a->misc.line);

            return OUT; }

          default:
            fail (Error_Cannot_Reflect(REB_ACTION, property));
        }
        break; }

      default:
        break;
    }

    return R_UNHANDLED;
}
