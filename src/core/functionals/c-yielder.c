//
//  File: %c-yielder.c
//  Summary: "Routines for Creating Coroutine Functions via Stackless Methods"
//  Section: datatypes
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2022 Ren-C Open Source Contributors
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
// Generators utilize the ability of the system to suspend and resume stacks.
//
// !!! This is a work-in-progress; true stackless generators are a problem
// that is conceptually as difficult to manage as multithreading.  There are
// issues with holding locks on arrays being enumerated which may be shared
// between the generators and other code, as well as the question of when to
// garbage-collect a generator.  Really this is just a proof-of-concept to
// show the unplugging and replugging of stacks.

#include "sys-core.h"


enum {
    IDX_YIELDER_BODY = 1,  // Push_Continuation_Details_0() uses details[0]
    IDX_YIELDER_MODE = 2,  // can't be frame spare (that's reset each call!)
    IDX_YIELDER_LAST_YIELDER_CONTEXT = 3,  // frame stack fragment to resume
    IDX_YIELDER_LAST_YIELD_RESULT = 4,  // so that `z: yield 1 + 2` is useful
    IDX_YIELDER_PLUG = 5,  // saved if you YIELD, captures data stack etc.
    IDX_YIELDER_META_OUT = 6,  // whatever L->out was when interrupted
    IDX_YIELDER_MAX
};

STATIC_ASSERT((int)IDX_YIELDER_BODY == (int)IDX_NATIVE_BODY);

enum {
    ST_YIELDER_WAS_INVOKED = 0,
    ST_YIELDER_IS_YIELDING,
    ST_YIELDER_RUNNING_BODY
};


//
//  Yielder_Dispatcher: C
//
// A yielder is a function instance which is made by a generator, that keeps
// a memory of the frame state it was in.  YIELD packs up the frame in a
// restartable way and unwinds it, allowing the continuation to request
// that be the frame that gets executed in the continuation.
//
Bounce Yielder_Dispatcher(Level* const L)
{
    USE_LEVEL_SHORTHANDS (L);

    Details* details = Phase_Details(PHASE);
    Value* mode = Details_At(details, IDX_YIELDER_MODE);

    switch (STATE) {
      case ST_YIELDER_WAS_INVOKED: goto invoked;
      case ST_YIELDER_IS_YIELDING: return OUT;
      case ST_YIELDER_RUNNING_BODY: goto body_finished_or_threw;
      default: assert(false);
    }

  invoked: {  ////////////////////////////////////////////////////////////////

    // Because yielders accrue state as they run, more than one can't be in
    // flight at a time.  Hence what would usually be an "initial entry" of
    // a new call for other dispatchers, each call is effectively to the same
    // "instance" of this yielder.  So the Phase_Details() is modified while
    // running, and it's the `state` we pay attention to.

    if (Is_Quasi_Blank(mode))  // currently on the stack and running
        fail ("Yielder was re-entered");

    if (Is_Boolean(mode)) {  // terminated due to finishing the body or error
        if (Cell_True(mode))  // terminated due to finishing the body
            return nullptr;

        fail ("Yielder called again after raising an error");
    }

    if (Is_Frame(mode))  // we were suspended by YIELD, and want to resume
        goto resume_body;

    assert(Is_Blank(mode));  // set by the YIELDER creation routine
    goto first_run;

} first_run: {  //////////////////////////////////////////////////////////////

    // Whatever we pass through here as the specifier has to stay working,
    // because it will be threaded and preserved in variables by the
    // running code (also, it's the binding of the YIELD statement, which
    // needs to be able to find the right frame).
    //
    // If there is no yield, we want a callback so we can mark the
    // generator as finished.
    //
    Value* body = Details_At(details, IDX_DETAILS_1);  // code to run

    Init_Quasi_Blank(mode);  // indicate "running"
    STATE = ST_YIELDER_RUNNING_BODY;

    return CONTINUE_CORE(
        OUT,  // body evaluative result
        ACTION_EXECUTOR_FLAG_DISPATCHER_CATCHES,  // can't resume after failure
        cast(Context*, L->varlist), body
    );

} resume_body: {  ////////////////////////////////////////////////////////////

    assert(Is_Frame(mode));

    Level* yielder_level = L;  // alias for clarity
    Level* yield_level = CTX_LEVEL_IF_ON_STACK(VAL_CONTEXT(mode));
    assert(yield_level != nullptr);

    // The YIELD binding pointed to the context varlist we used in the
    // original yielder dispatch.  That completed--but we need to reuse
    // the identity for this new yielder frame for the YIELD to find it
    // in the stack walk.
    //
    Context* last_yielder_context = VAL_CONTEXT(
        Details_At(details, IDX_YIELDER_LAST_YIELDER_CONTEXT)
    );

    // We want the identity of the old varlist to replace this yielder's
    // varlist identity.  But we want the frame's values to reflect the
    // args the user passed in to this invocation of the yielder.  So move
    // those into the old varlist before replacing this varlist with that
    // prior identity.
    //
    const Key* key_tail;
    const Key* key = CTX_KEYS(&key_tail, last_yielder_context);
    Param* param = ACT_PARAMS_HEAD(Level_Phase(yielder_level));
    Value* dest = CTX_VARS_HEAD(last_yielder_context);
    Value* src = Level_Args_Head(yielder_level);
    for (; key != key_tail; ++key, ++param, ++dest, ++src) {
        if (Is_Specialized(param))
            continue;  // don't overwrite locals (including YIELD)0
        Move_Cell(dest, src);  // all arguments/refinements are fair game
    }

    // With variables extracted, we no longer need the varlist for this
    // invocation (wrong identity) so we free it, if it isn't GC-managed,
    // as it wouldn't get freed otherwise.
    //
/*    if (Not_Node_Managed(yielder_level->varlist)) {
        //
        // We only want to kill off this one frame; but the GC will think
        // that we want to kill the whole stack of frames if we don't
        // zero out the keylist node.
        //
        LINK(yielder_level->varlist).custom.node = nullptr;

        GC_Kill_Flex(yielder_level->varlist);  // Note: no tracking
    } */

    // When the last yielder dropped from the frame stack, it should have
    // decayed its keysource from a REBFRM* to the action that was
    // invoked (which could be an arbitrary specialization--e.g. different
    // variants of the yielder with different f_original could be used
    // between calls).  This means we can only compare underlying actions.
    //
    // Now we have a new REBFRM*, so we can reattach the context to that.
    //
/*    assert(
        ACT_UNDERLYING(cast(Action*, BONUS(KeySource, last_yielder_context)))
        == ACT_UNDERLYING(yielder_level->u.action.original)
    ); */
    INIT_BONUS_KEYSOURCE(CTX_VARLIST(last_yielder_context), yielder_level);

    // Now that the last call's context varlist is pointing at our current
    // invocation level, we point the other way from the level to the
    // varlist.  We also update the cached pointer to the rootvar of that
    // frame (used to speed up Level_Phase() and Level_Coupling())
    //
    L->varlist = CTX_VARLIST(last_yielder_context);  // rootvar must match
    L->rootvar = m_cast(Element*, CTX_ARCHETYPE(last_yielder_context));

    Value* plug = Details_At(details, IDX_YIELDER_PLUG);
    Replug_Stack(yield_level, yielder_level, plug);
    Assert_Is_Unreadable_If_Debug(plug);  // Replug wiped, make GC safe

    // Restore the in-progress output cell state that was going on when
    // the YIELD ran (e.g. if it interrupted a CASE or something, this
    // would be what the case had in the out cell at moment of interrupt).
    //
    Value* out_copy = Details_At(details, IDX_YIELDER_META_OUT);
    Move_Cell(yielder_level->out, out_copy);
    Meta_Unquotify_Undecayed(yielder_level->out);

    // We could make YIELD appear to return a VOID! when we jump back in
    // to resume it.  But it's more interesting to return what the YIELD
    // received as an arg (YIELD cached it in details before jumping)
    //
    Move_Cell(
        yield_level->out,
        Details_At(details, IDX_YIELDER_LAST_YIELD_RESULT)
    );

    // If the yielder actually reaches its end (instead of YIELD-ing)
    // we need to know, so we can mark that it is finished.
    //
    assert(Not_Executor_Flag(ACTION, yielder_level, DELEGATE_CONTROL));

    Level_State_Byte(yielder_level) = ST_YIELDER_RUNNING_BODY;  // set again
    Set_Executor_Flag(ACTION, yielder_level, DISPATCHER_CATCHES);  // set again
    Init_Quasi_Blank(mode);  // indicate running
    return BOUNCE_CONTINUE;  // ...resuming where we left off (was DEWIND)

} body_finished_or_threw: {  /////////////////////////////////////////////////

    assert(L == TOP_LEVEL);
    assert(Level_State_Byte(TOP_LEVEL) != 0);

    // Clean up all the details fields so the GC can reclaim the memory
    //
    Init_Unreadable(Details_At(details, IDX_YIELDER_LAST_YIELDER_CONTEXT));
    Init_Unreadable(Details_At(details, IDX_YIELDER_LAST_YIELD_RESULT));
    Init_Unreadable(Details_At(details, IDX_YIELDER_PLUG));
    Init_Unreadable(Details_At(details, IDX_YIELDER_META_OUT));

 /*   if (Is_Throwing(L)) {
        if (Is_Throwing_Failure(L)) {
            //
            // We treat a failure as if it was an invalid termination of the
            // yielder.  Future calls will raise an error.
            //
            Init_False(mode);
        }
        else {
            // We treat a throw as if it was a valid termination of the
            // yielder (e.g. a RETURN which crosses out of it).  Future calls
            // will return NULL.
            //
            Init_True(mode);
        }
        return THROWN;
    } */

    Init_True(mode);  // finished successfully
    return nullptr;  // the true signals return NULL for all future calls
}}


//
//  yielder: native [
//
//      return: "Action that can be called repeatedly until it yields NULL"
//          [action?]
//      spec "Arguments passed in to each call for the generator"
//          [block!]
//      body "Code containing YIELD statements"
//          [block!]
//  ]
//
DECLARE_NATIVE(yielder)
{
    INCLUDE_PARAMS_OF_YIELDER;

    Element* spec = cast(Element*, ARG(spec));

    // We start by making an ordinary-seeming interpreted function, but that
    // has a local "yield" which is bound to the frame upon execution.
    //
    Value* body = rebValue("compose [",
        "let yield: runs bind get $lib/yield binding of $return",
        "(as group!", ARG(body), ")",  // GROUP! so it can't backquote 'YIELD
    "]");

    Phase* yielder = Make_Interpreted_Action_May_Fail(
        spec,
        cast(Element*, body),
        MKF_RETURN,  // give it a RETURN
        &Yielder_Dispatcher,
        IDX_YIELDER_MAX  // details array capacity
    );
    rebRelease(body);

    Details* details = Phase_Details(yielder);

    assert(Is_Block(Array_At(details, IDX_YIELDER_BODY)));
    Init_Blank(Details_At(details, IDX_YIELDER_MODE));  // starting
    Init_Unreadable(Details_At(details, IDX_YIELDER_LAST_YIELDER_CONTEXT));
    Init_Unreadable(Details_At(details, IDX_YIELDER_LAST_YIELD_RESULT));
    Init_Unreadable(Details_At(details, IDX_YIELDER_PLUG));
    Init_Unreadable(Details_At(details, IDX_YIELDER_META_OUT));

    return Init_Action(OUT, yielder, ANONYMOUS, UNBOUND);
}


//
//  generator: native [
//
//      return: "Arity-0 action you can call repeatedly until it yields NULL"
//          [action?]
//      body "Code containing YIELD statements"
//          [block!]
//  ]
//
DECLARE_NATIVE(generator)
{
    INCLUDE_PARAMS_OF_GENERATOR;

    return rebValue(Canon(YIELDER), EMPTY_BLOCK, ARG(body));
}


//
//  yield: native [
//
//  "Function used with GENERATOR and YIELDER to give back results"
//
//      return: "Same value given as input (NULL/VOID enclosed in pack)"
//          [any-value?]
//      value "Value to yield"
//          [any-value?]
//  ]
//
DECLARE_NATIVE(yield)
//
// The benefits of distinguishing NULL as a generator result meaning the body
// has completed are considered to outweigh the ability to yield pure NULL.
// A modified generator that yields quoted values and unquotes on exit points
// can be used to work around this.
{
    INCLUDE_PARAMS_OF_YIELD;

    enum {
        ST_YIELD_WAS_INVOKED = 0,
        ST_YIELD_YIELDED
    };

    switch (STATE) {
      case ST_YIELD_WAS_INVOKED: goto invoked;
      case ST_YIELD_YIELDED: return OUT;
      default: assert(false);
    }

  invoked: {  ////////////////////////////////////////////////////////////////

    assert(level_ == TOP_LEVEL);  // level_ is an implicit arg to natives
    assert(Level_Phase(level_) == ACT_IDENTITY(VAL_ACTION(Lib(YIELD))));
    Level* yield_level = level_;  // ...make synonyms more obvious

    Context* yielder_context = maybe Level_Coupling(yield_level);
    if (not yielder_context)
        fail ("Must have yielder to jump to");

    Level* yielder_level = CTX_LEVEL_MAY_FAIL(yielder_context);
    if (not yielder_level)
        fail ("Cannot yield to generator that has completed");

    Phase* yielder_phase = Level_Phase(yielder_level);
    assert(ACT_DISPATCHER(yielder_phase) == &Yielder_Dispatcher);

    // !!! How much sanity checking should be done before doing the passing
    // thru of the NULL?  Err on the side of safety first, and don't let NULL
    // be yielded to the unbound archetype or completed generators.
    //
    if (Is_Nulled(ARG(value)))
        return nullptr;

    Details* yielder_details = Phase_Details(yielder_phase);

    // Evaluations will frequently use the L->out to accrue state, perhaps
    // preloading with something (like NULL) that is expected to be there.
    // But we're interrupting the frame and returning what YIELD had instead
    // of that evaluative product.  It must be preserved.  But since we can't
    // put END values in blocks, use the hidden block to indicate that
    //
    Value* out_copy = Details_At(yielder_details, IDX_YIELDER_META_OUT);
    Copy_Meta_Cell(out_copy, yielder_level->out);

    Value* plug = Details_At(yielder_details, IDX_YIELDER_PLUG);
    Assert_Is_Unreadable_If_Debug(plug);
    Unplug_Stack(plug, yield_level, yielder_level);

    // We preserve the fragment of call stack leading from the yield up to the
    // yielder in a FRAME! value that the yielder holds in its `details`.
    // The garbage collector should notice it is there, and mark it live up
    // until the nullptr that we put at the root.
    //
    Value* mode = Details_At(yielder_details, IDX_YIELDER_MODE);
    assert(Is_Quasi_Blank(mode));  // should be signal for "currently running"
    Init_Frame(mode, Context_For_Level_May_Manage(yield_level), ANONYMOUS);
    Assert_Flex_Managed(VAL_CONTEXT(mode));
    assert(CTX_LEVEL_IF_ON_STACK(VAL_CONTEXT(mode)) == yield_level);

    // We store the frame chain into the yielder, as a FRAME! value.  The
    // GC of the ACTION's details will keep it alive.
    //
    Init_Frame(
        Details_At(yielder_details, IDX_YIELDER_LAST_YIELDER_CONTEXT),
        yielder_context,
        ANONYMOUS
    );

    // The Init_Frame() should have managed the yielder_level varlist, which
    // means that when the yielder does Drop_Level() yielder_context survives.
    // It should decay the keysource from a REBFRM* to the action paramlist,
    // but the next run of the yielder will swap in its new REBFRM* over that.
    //
    assert(CTX_VARLIST(yielder_context) == yielder_level->varlist);
    Assert_Flex_Managed(yielder_level->varlist);

    // We don't only write the yielded value into the output slot so it is
    // returned from the yielder.  We also stow an extra copy of the value
    // into the yielder details, which we use to make it act act as the
    // apparent return result of the YIELD when the yielder is called again.
    //
    //    x: yield 1 + 2
    //    print [x]  ; could be useful if this was 3 upon resumption, right?
    //
    Copy_Cell(yielder_level->out, ARG(value));
    Move_Cell(
        Details_At(yielder_details, IDX_YIELDER_LAST_YIELD_RESULT),
        ARG(value)
    );

    /* REBACT *target_fun = FRM_UNDERLYING(target_frame); */

    Level_State_Byte(yielder_level) = ST_YIELDER_IS_YIELDING;

    STATE = ST_YIELD_YIELDED;
    return BOUNCE_CONTINUE;  // was DEWIND
}}
