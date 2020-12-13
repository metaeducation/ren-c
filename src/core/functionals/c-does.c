//
//  File: %c-does.c
//  Summary: "Expedient generator for 0-argument function specializations"
//  Section: datatypes
//  Project: "Ren-C Language Interpreter and Run-time Environment"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2018-2020 Ren-C Open Source Contributors
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
// DOES in historical Rebol was simply a specialization of FUNC which assumed
// an empty spec block as a convenience.  It was thus in most other respects
// like a FUNC... e.g. it would catch throws of a RETURN signal.
//
// Ren-C experimentally pushes DOES a bit further.  Not only does it take
// blocks, but it can take any other data type that DO will accept...such as
// a FILE! or URL!:
//
//     >> d: does https://example.com/some-script.reb
//
//     >> d
//     ; Will act like `do https://example/some-script.reb`
//
// If it takes a block, it will treat it in a relativized way (as with FUNC)
// but it will not catch returns.  This means RETURN will be left bound as is.
// (Those who prefer getting RETURNs can just do `FUNC [] [...]`, this offers
// a unique alternative to that.)
//
// Further, it tries to allow you to specialize all of a function's arguments
// at once inline:
//
//     >> c: does catch [throw <like-this>]
//
//     >> c
//     == <like-this>
//
// !!! The fast specialization behavior of DOES is semi-related to POINTFREE,
// and was initially introduced for its potential usage in code golf.  This
// feature has not been extensively used or tested.  Review.
//

#include "sys-core.h"

enum {
    IDX_DOES_BLOCK = 1,  // Special case of BLOCK! to be executed
    IDX_DOES_MAX
};


//
//  Block_Dispatcher: C
//
// There are no arguments or locals to worry about in a DOES, nor does it
// heed any definitional RETURN.  This means that in many common cases we
// don't need to do anything special to a BLOCK! passed to DO...no copying
// or otherwise.  Just run it when the function gets called.
//
// Yet `does [...]` isn't *quite* like `specialize :do [source: [...]]`.  The
// difference is subtle, but important when interacting with bindings to
// fields in derived objects.  That interaction cannot currently resolve such
// bindings without a copy, so it is made on demand.
//
// (Luckily these copies are often not needed, such as when the DOES is not
// used in a method... -AND- it only needs to be made once.)
//
REB_R Block_Dispatcher(REBFRM *f)
{
    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    assert(ARR_LEN(details) == IDX_DOES_MAX);

    RELVAL *block = STABLE(ARR_AT(details, IDX_DOES_BLOCK));
        // ^-- note not a `const RELVAL *block`, may get updated!
    assert(IS_BLOCK(block) and VAL_INDEX(block) == 0);

    if (IS_SPECIFIC(block)) {
        if (FRM_BINDING(f) == UNBOUND) {
            if (Do_Any_Array_At_Throws(f->out, SPECIFIC(block), SPECIFIED))
                return R_THROWN;
            return f->out;
        }

        // Until "virtual binding" is implemented, we would lose f->binding's
        // ability to influence any variable lookups in the block if we did
        // not relativize it to this frame.  This is the only current way to
        // "beam down" influence of the binding for cases like:
        //
        // What forces us to copy the block are cases like this:
        //
        //     o1: make object! [a: 10 b: does [if true [a]]]
        //     o2: make o1 [a: 20]
        //     o2/b = 20
        //
        // While o2/b's ACTION! has a ->binding to o2, the only way for the
        // [a] block to get the memo is if it is relative to o2/b.  It won't
        // be relative to o2/b if it didn't have its existing relativism
        // Derelativize()'d out to make it specific, and then re-relativized
        // through a copy on behalf of o2/b.

        REBARR *body_array = Copy_And_Bind_Relative_Deep_Managed(
            SPECIFIC(block),
            FRM_PHASE(f),
            TS_WORD,
            false  // do not gather LETs
        );

        // Preserve file and line information from the original, if present.
        //
        if (GET_ARRAY_FLAG(VAL_ARRAY(block), HAS_FILE_LINE_UNMASKED)) {
            LINK_FILE_NODE(body_array) = LINK_FILE_NODE(VAL_ARRAY(block));
            MISC(body_array).line = MISC(VAL_ARRAY(block)).line;
            SET_ARRAY_FLAG(body_array, HAS_FILE_LINE_UNMASKED);
        }

        // Update block cell as a relativized copy (we won't do this again).
        //
        REBACT *phase = FRM_PHASE(f);
        Init_Relative_Block(block, phase, body_array);
    }

    assert(IS_RELATIVE(block));

    if (Do_Any_Array_At_Throws(f->out, block, SPC(f->varlist)))
        return R_THROWN;

    return f->out;
}


//
//  does: native [
//
//  {Specializes DO for a value (or for args of another named function)}
//
//      return: [action!]
//      :specializee [any-value!]
//          {WORD! or PATH! names function to specialize, else arg to DO}
//      'args [any-value! <variadic>]
//          {arguments which will be consumed to fulfill a named function}
//  ]
//
REBNATIVE(does)
{
    INCLUDE_PARAMS_OF_DOES;

    REBVAL *specializee = ARG(specializee);

    if (IS_BLOCK(specializee)) {
        REBARR *paramlist = Make_Array_Core(
            1,  // archetype only...DOES always makes action with no arguments
            SERIES_MASK_PARAMLIST
        );

        Voidify_Rootparam(paramlist);
        TERM_ARRAY_LEN(paramlist, 1);

        // `does [...]` and `does do [...]` are not exactly the same.  The
        // generated ACTION! of the first form uses Block_Dispatcher() and
        // does on-demand relativization, so it's "kind of like" a `func []`
        // in forwarding references to members of derived objects.  Also, it
        // is optimized to not run the block with the DO native...hence a
        // HIJACK of DO won't be triggered by invocations of the first form.
        //
        Manage_Array(paramlist);
        REBACT *doer = Make_Action(
            paramlist,
            nullptr,  // no meta (REDESCRIBE can add help)
            &Block_Dispatcher,  // **SEE COMMENTS**, not quite like plain DO!
            nullptr,  // no specialization exemplar (or inherited exemplar)
            IDX_DOES_MAX  // details array capacity
        );

        // Block_Dispatcher() *may* copy at an indeterminate time, so to keep
        // things invariant we have to lock it.
        //
        unstable RELVAL *body = ARR_AT(ACT_DETAILS(doer), IDX_DOES_BLOCK);
        Force_Value_Frozen_Deep(specializee);
        Move_Value(body, specializee);

        return Init_Action(D_OUT, doer, ANONYMOUS, UNBOUND);
    }

    REBCTX *exemplar;
    const REBSTR *label;
    if (
        GET_CELL_FLAG(specializee, UNEVALUATED)
        and (IS_WORD(specializee) or IS_PATH(specializee))
    ){
        if (Make_Frame_From_Varargs_Throws(
            D_OUT,
            specializee,
            ARG(args)
        )){
            return R_THROWN;
        }
        exemplar = VAL_CONTEXT(D_OUT);
        label = VAL_FRAME_LABEL(D_OUT);
    }
    else {
        // On all other types, we just make it act like a specialized call to
        // DO for that value.

        exemplar = Make_Context_For_Action(
            NATIVE_VAL(do),
            DSP,  // lower dsp would be if we wanted to add refinements
            nullptr  // don't set up a binder; just poke specializee in frame
        );
        assert(GET_SERIES_FLAG(exemplar, MANAGED));

        // Put argument into DO's *second* frame slot (first is RETURN)
        //
        assert(VAL_KEY_SYM(CTX_KEY(exemplar, 1)) == SYM_RETURN);
        Move_Value(CTX_VAR(exemplar, 2), specializee);
        Move_Value(specializee, NATIVE_VAL(do));
        label = ANONYMOUS;
    }

    REBACT *doer = Make_Action_From_Exemplar(exemplar);
    return Init_Action(D_OUT, doer, label, UNBOUND);
}
