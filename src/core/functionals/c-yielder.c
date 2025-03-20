//
//  File: %c-yielder.c
//  Summary: "Routines for Creating Coroutine Functions via Stackless Methods"
//  Section: datatypes
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2020-2024 Ren-C Open Source Contributors
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


//
//  Startup_Yielder_Errors: C
//
void Startup_Yielder_Errors(void)
{
    ensure(nullptr, g_error_done_enumerating) = Init_Error(
        Alloc_Value(),
        Error_Done_Enumerating_Raw()
    );
}


//
//  Shutdown_Yielder_Errors: C
//
void Shutdown_Yielder_Errors(void)
{
    rebReleaseAndNull(&g_error_done_enumerating);
}


//
//  Is_Error_Done_Signal: C
//
bool Is_Error_Done_Signal(const Cell* c) {
    assert(Cell_Heart(c) == TYPE_ERROR);

    ERROR_VARS *vars = ERR_VARS(Cell_Error(c));
    if (not Is_Word(&vars->id))
        return false;
    return Cell_Word_Id(&vars->id) == SYM_DONE_ENUMERATING;
}


//
//  /done: native [
//
//  "Give back a raised error with the id DONE-ENUMERATING (pass to YIELD)"
//
//      return: [raised!]
//  ]
//
DECLARE_NATIVE(DONE) {
    INCLUDE_PARAMS_OF_DONE;

    Copy_Cell(OUT, g_error_done_enumerating);
    return Raisify(OUT);
}


//
//  /done?: native:intrinsic [
//
//  "Detect whether argument is the DONE-ENUMERATING raised error"
//
//      return: [logic?]
//      ^atom
//  ]
//
DECLARE_NATIVE(DONE_Q) {
    INCLUDE_PARAMS_OF_DONE_Q;

    DECLARE_ELEMENT (meta);
    Get_Meta_Atom_Intrinsic(meta, LEVEL);

    if (not Is_Meta_Of_Raised(meta))
        return nullptr;

    return LOGIC(Is_Error_Done_Signal(meta));
}



enum {
    IDX_YIELDER_BODY = IDX_INTERPRETED_BODY,  // see Make_Interpreted_Action()
    IDX_YIELDER_ORIGINAL_FRAME,  // varlist identity to steal on resume
    IDX_YIELDER_PLUG,  // saved when you YIELD (captures data stack etc.)
    IDX_YIELDER_META_YIELDED,  // the argument YIELD was passed
    MAX_IDX_YIELDER = IDX_YIELDER_META_YIELDED
};

//=//// YIELDER STATE BYTE (DIFFERENT: VERY LIMITED!) /////////////////////=//
//
// The STATE byte in a Level is usually quite useful to continuation-based
// functions, to know what "mode" they are in.  They always start at STATE_0
// and then bump the STATE along through various steps, until they calculate
// a final result and return it in OUT.
//
// But Yielders (and Generators) are weird, because the same "conceptual"
// function is being called and returning a value in OUT many times.  Each
// call starts over at STATE_0.  This means the actual knowledge of where
// things are in a particular yielder instance's process has to be stored
// in the Details array, in order to persist between invocations.
//
// So really, there's only two state bytes.  One indicates a new invocation
// that has to check to see if the Details array contains information
// suggests it should start running the body or resume it.  And the other
// is the state an invocation goes to when it is in flight, and needs to
// catch YIELDs or termination of the body.
//
enum {
    ST_YIELDER_INVOKED = 0,  // could be initial invocation, or subsequent
    ST_YIELDER_RUNNING_BODY
};


//=//// YIELD STATE BYTE //////////////////////////////////////////////////=//
//
// The YIELD statement itself is simpler.  When it gets called, it knows its
// associated YIELDER based on the "Coupling" stored in the Action Cell.
// So it suspends the stack state between it and the yielder and puts that
// suspended "plug" into the yielder's Details, while its own Level gets
// stored as part of the plug.
//
// When the plug is restored by the next call to the yielder, it pushes the
// YIELD's Level back on the stack.  That might seem like a good time for
// Yielder_Dispatcher() to return the yield_level->out, to signal that the
// YIELD has finished so it doesn't need a Trampoline bounce.  But the
// Action_Executor() is not expecting the Level to change out from under
// it (at time of writing), unless it returns BOUNCE_CONTINUE.  So there has
// to be another state of ST_YIELD_SUSPENDED for the Yield's Dispatcher to get
// called with and return OUT.
//
// (This isn't perfectly efficient, and the balance of making it possible
// for the Action_Executor() to tolerate returning the TOP_LEVEL's OUT when
// it is not the Level it dispatched is something that could be considered,
// but this works for now.)
//
enum {
    ST_YIELD_INITIAL_ENTRY = STATE_0,
    ST_YIELD_SUSPENDED
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

    assert(L == TOP_LEVEL);

    Details* details = Ensure_Level_Details(L);

    Value* body = Details_At(details, IDX_YIELDER_BODY);
    Value* original_frame = Details_At(details, IDX_YIELDER_ORIGINAL_FRAME);
    Value* plug = Details_At(details, IDX_YIELDER_PLUG);
    Value* meta_yielded = Details_At(details, IDX_YIELDER_META_YIELDED);

    switch (STATE) {  // Can't use STATE byte for "mode" (see ST_YIELDER enum)
      case ST_YIELDER_INVOKED: {
        if (Not_Cell_Readable(original_frame))
            goto begin_body;  // first run, haven't set original frame yet

        if (Is_Frame(original_frame))
            goto resume_body_if_not_reentrant;

        if (Is_Blank(original_frame))
            goto invoke_completed_yielder;

        assert(Is_Trash(original_frame));
        goto invoke_yielder_that_abruptly_failed; }

      case ST_YIELDER_RUNNING_BODY: {
        if (Is_Cell_Readable(meta_yielded))  // YIELD is suspending us
            goto yielding;

        goto body_finished_or_threw; }

      default:
        assert(false);
    }

  begin_body: {  /////////////////////////////////////////////////////////////

    // 1. Many calls can be made to a yielder, with each call having a new
    //    FRAME!'s worth of arguments (and a new VarList identity for that
    //    frame).  However, when we poke an identity into the definitional
    //    YIELDs that are associated with a yielder, that YIELD could be
    //    copied or stowed places besides in the Yielder's frame...and
    //    they need to still work.  So resumptions need to take over the
    //    original VarList identity each time, moving their arguments in to
    //    overwrite the last call.  Stow that original VarList in Details.
    //
    // 2. We can't fire-and-forget to run the yielder body, because we have
    //    to clean up the Details array on completion or a throw/failure.
    //    That cleanup isn't just to free things up for the GC, but also to
    //    make sure future attempts to invoke the yielder see information in
    //    the Details array telling them it has finished.
    //
    // 3. We use CONTINUE() instead of DELEGATE() because we need a call
    //    back when the body finishes running.  The result of the body
    //    evaluation is not used, so we write it into SPARE (which must be
    //    preserved in suspension).

    assert(Not_Cell_Readable(original_frame));  // each call needs [1]
    Force_Level_Varlist_Managed(L);
    Init_Frame(original_frame, Level_Varlist(L), Level_Label(L), NONMETHOD);

    Inject_Definitional_Returner(L, LIB(DEFINITIONAL_YIELD), SYM_YIELD);

    assert(Is_Block(body));  // can mutate (only one call)
    Add_Link_Inherit_Bind(L->varlist, Cell_List_Binding(body));
    Tweak_Cell_Binding(body, L->varlist);

    STATE = ST_YIELDER_RUNNING_BODY;

    Enable_Dispatcher_Catching_Of_Throws(L);  // need to finalize on throws [2]
    return CONTINUE(SPARE, body);  // need finalize, body result discarded [3]

} yielding: {  ///////////////////////////////////////////////////////////////

    // When YIELD is called, it unplugs the stack and stores it in the
    // YIELDER_PLUG slot of the Yielder's Details.  After it has done this,
    // the Yielder_Dispatcher()'s Level becomes the top of the stack, and
    // gets bounced to, and it's what bubbles the value in OUT.

    if (Not_Cell_Readable(plug)) {  // no plug, must be YIELD of a RAISED...
        assert(Is_Meta_Of_Raised(meta_yielded));

        if (Is_Error_Done_Signal(meta_yielded)) {
            // don't promote to failure, just consider it finished
        }
        else {  // all other raised errors promoted to failure
            Init_Thrown_Failure(L, Cell_Error(meta_yielded));
        }
        goto body_finished_or_threw;
    }

    assert(not Is_Meta_Of_Raised(meta_yielded));
    assert(Is_Handle(plug));

    Copy_Cell(OUT, meta_yielded);  // keep meta_yielded around for resume
    return Meta_Unquotify_Undecayed(OUT);

} resume_body_if_not_reentrant: {  ///////////////////////////////////////////

    // If we're given a request for an invocation that isn't the initial
    // invocation, and there's no stored "plug" of suspended levels, then
    // that means the code isn't suspended.  So it's something like:
    //
    //     >> g: generator [g]  ; not legal!

    if (Not_Cell_Readable(plug))
        return FAIL(Error_Yielder_Reentered_Raw());

  //=//// RECLAIM ORIGINAL YIELDER'S VARLIST IDENTITY /////////////////////=//

    // 1. We want the identity of the old varlist to replace this yielder's
    //    varlist identity.  But we want the frame's values to reflect the
    //    args the user passed in to this invocation of the yielder.  So move
    //    those into the old varlist before replacing this varlist with that
    //    prior identity.
    //
    // 2. With variables extracted, we no longer need the varlist for this
    //    invocation (wrong identity) so we free it, if it isn't GC-managed,
    //    as it wouldn't get freed otherwise.
    //
    // 3. It may seem like there'd be no reason for the varlist to become
    //    managed (Yielder_Dispatcher() is the native dispatcher, and in this
    //    case we're not putting the VarList into a YIELD cell or doing
    //    anything to otherwise manage it).  But things like ENCLOSE or
    //    other operations can lead to the VarList being managed before it
    //    gets to Yielder_Dispatcher(), so if it did we can't free it, but
    //    just decay it minimally down to a Stub.)
    //
    // 3. Now that the last call's context varlist is pointing at our current
    //    invocation level, we point the other way from the level to the
    //    varlist.  We also update the cached pointer to the rootvar of that
    //    frame (used to speed up Level_Phase() and Level_Coupling())

    Level* yielder_level = L;  // alias for clarity
    VarList* original_varlist = Cell_Varlist(original_frame);

    const Key* key_tail;  // move this yielder call frame into old varlist [1]
    const Key* key = Varlist_Keys(&key_tail, original_varlist);
    Param* param = Phase_Params_Head(Level_Phase(L));
    Value* dest = Varlist_Slots_Head(original_varlist);
    Value* src = Level_Args_Head(L);
    for (; key != key_tail; ++key, ++param, ++dest, ++src) {
        if (Is_Specialized(param))
            continue;  // don't overwrite locals (including definitional YIELD)
        Move_Cell(dest, src);  // all arguments/refinements are fair game
    }

    if (Not_Node_Managed(L->varlist))  // don't need it [2]
        GC_Kill_Flex(L->varlist);
    else
        Decay_Stub(L->varlist);  // maybe lingering references [3]

    Tweak_Misc_Runlevel(original_varlist, L);  // [4]
    L->varlist = Varlist_Array(original_varlist);  // rootvar must match
    L->rootvar = m_cast(Element*, Varlist_Archetype(original_varlist));

  //=//// RESUME THE YIELD-SUSPENDED STATE ////////////////////////////////=//

    // 1. Restore the in-progress SPARE state that was going on when the YIELD
    //    ran (e.g. if it interrupted a CASE or something, it could have held
    //    state in its OUT cell which would be the Level's SPARE, that goes
    //    away when that Level is destroyed.  We have to is would be what
    //    the case had in the OUT cell at moment of interrupt,
    //    which may have been a meaningful state).
    //
    // 2. We could make YIELD appear to return void when we jump back in
    //    to resume it.  But it's more interesting to return what the YIELD
    //    received as an arg (YIELD cached it in details before unwinding).

    Replug_Stack(yielder_level, plug);
    assert(Not_Cell_Readable(plug));  // Replug wiped, make GC safe

    Level* yield_level = TOP_LEVEL;
    assert(yield_level != L);
    assert(LEVEL_STATE_BYTE(yield_level) == ST_YIELD_SUSPENDED);

    Copy_Cell(yield_level->out, meta_yielded);  // resumed YIELD's result [2]
    Meta_Unquotify_Undecayed(yield_level->out);
    Init_Unreadable(meta_yielded);

    assert(STATE == ST_YIELDER_INVOKED);
    STATE = ST_YIELDER_RUNNING_BODY;  // resume where the last YIELD left off

    Enable_Dispatcher_Catching_Of_Throws(L);  // need to finalize on throws
    return BOUNCE_CONTINUE;  // see notes in begin_body about CONTINUE + catch

} body_finished_or_threw: {  /////////////////////////////////////////////////

    // 1. It's a question as to whether to error or not if you do something
    //    like THROW out of a yielder or generator:
    //
    //        catch [g: generator [yield 1, throw 20, print "???"], g, g]
    //
    //    Throwing destroys the evaluation state, and you can't bring it
    //    back to make another call.  But should it be considered a
    //    successful completion?  A THROW of this nature in a normal
    //    function running its body would be all right, so we go by that
    //    and say that cooperative (non-abrupt-fail) throws are valid
    //    ways to signal the yielder is finished.
    //
    // 2. There are some big picture issues about the garbage collection of
    //    yielders and generators that don't get run to completion--because
    //    there's really nothing that will clean them up.  Do what we can
    //    here, at least, and reduce the GC burden when they do complete
    //    by clearing out references to frames and the original body.
    //
    // 3. When you have (g: generator [yield 1, yield 2, append [a b] 'c])
    //    one might ask if the third call to G should yield [a b c], or
    //    be like a function and yield trash (~), or just be considered
    //    an end state.  End state makes the most sense by far.

    assert(Is_Block(body));  // clean up details for GC [2]
    Init_Unreadable(body);
    assert(Not_Cell_Readable(plug));
    assert(Not_Cell_Readable(meta_yielded));

    assert(Is_Frame(original_frame));

    if (not Is_Throwing(L)) {
        Init_Blank(original_frame);  // body reached end, signal completed [3]
        goto invoke_completed_yielder;
    }

    if (Is_Throwing_Failure(L)) {  // abrupt fail inside yielder
        Init_Trash(original_frame);
        return THROWN;
    }

    const Value* label = VAL_THROWN_LABEL(L);
    if (
        Is_Frame(label)
        and Cell_Frame_Phase(label) == Cell_Frame_Phase(LIB(DEFINITIONAL_YIELD))
        and Cell_Frame_Coupling(label) == Level_Varlist(L)
    ){
        CATCH_THROWN(OUT, L);
        if (not Is_Meta_Of_Raised(OUT)) {  // THROW:FINAL value
            Init_Blank(original_frame);
            return Meta_Unquotify_Undecayed(OUT);  // done, this is last value
        }
        if (Is_Error_Done_Signal(OUT)) {
            Init_Blank(original_frame);
            goto invoke_completed_yielder;
        }
        Init_Trash(original_frame);
        Init_Thrown_Failure(L, Cell_Error(OUT));
        return THROWN;
    }

    Init_Blank(original_frame);  // THROW counts as completion [1]
    return THROWN;

} invoke_completed_yielder: {  ///////////////////////////////////////////////

    // Our signal of completion is the DONE-ENUMERATING definitional error.
    // Using a definitional error pushes it out of band from all other
    // return states, because any other raised error passed to YIELD is
    // handled as an abrupt failure.

    assert(Is_Blank(original_frame));

    Copy_Cell(OUT, g_error_done_enumerating);
    return Raisify(OUT);

} invoke_yielder_that_abruptly_failed: {  ////////////////////////////////////

    // A yielder that has abruptly failed currently does not store the error
    // that caused it to fail.  It conceivably could do so, and then every
    // subsequent call could keep returning that error...but that might
    // be misleading, suggesting that the error had happened again (when it
    // may represent something that would no longer be an error if the same
    // operation were tried).  Also, holding the error would prevent it from
    // garbage collecting.  So we instead just report a generic error about
    // a previous failure...which is probably better than conflating it
    // with saying that the yielder is done.

    assert(Is_Trash(original_frame));

    return FAIL(Error_Yielder_Failed_Raw());
}}


//
//  Yielder_Details_Querier: C
//
bool Yielder_Details_Querier(
    Sink(Value) out,
    Details* details,
    SymId property
){
    assert(Details_Dispatcher(details) == &Yielder_Dispatcher);
    assert(Details_Max(details) == MAX_IDX_YIELDER);

    switch (property) {

  //=//// RETURN ///////////////////////////////////////////////////////////=//

    // The "Return" from a Yielder is actually what its YIELD function gives
    // back...it always includes the definitional error for generator being
    // exhausted (how to add this legitimately?)

      case SYM_RETURN_OF: {
        Extract_Paramlist_Returner(out, Phase_Paramlist(details), SYM_YIELD);
        return true; }

      default:
        break;
    }

    return false;
}


//
//  /yielder: native [
//
//      return: "Action that can be called repeatedly until it yields NULL"
//          [action!]
//      spec "Arguments passed in to each call for the generator"
//          [block!]
//      body "Code containing YIELD statements"
//          [block!]
//      ; :resettable  ; should yielders offer a reset facility?
//  ]
//
DECLARE_NATIVE(YIELDER)
//
// 1. Having the generated yielder offer a :RESET that puts it back to the
//    initial state might be a useful feature.  Though not all generators
//    are resettable in terms of their semantics--just putting the function
//    back to the initial state is no guarantee that's enough that it can
//    do the enumeration again.
{
    INCLUDE_PARAMS_OF_YIELDER;

    Element* spec = Element_ARG(SPEC);
    Element* body = Element_ARG(BODY);

    Details* details = Make_Interpreted_Action_May_Fail(
        spec,
        body,  // relativized and put in Details array at IDX_YIELDER_BODY
        SYM_YIELD,  // give it a YIELD, but no RETURN (see YIELD:FINAL)
        &Yielder_Dispatcher,
        MAX_IDX_YIELDER  // details array capacity
    );

    assert(Is_Block(Details_At(details, IDX_YIELDER_BODY)));
    Init_Unreadable(Details_At(details, IDX_YIELDER_ORIGINAL_FRAME));
    Init_Unreadable(Details_At(details, IDX_YIELDER_PLUG));
    Init_Unreadable(Details_At(details, IDX_YIELDER_META_YIELDED));

    return Init_Action(OUT, details, ANONYMOUS, UNBOUND);
}


//
//  /generator: native [
//
//      return: "Arity-0 action you can call repeatedly until it yields NULL"
//          [action!]
//      :spec [] "internal use only"
//      body "Code containing YIELD statements"
//          [block!]
//  ]
//
DECLARE_NATIVE(GENERATOR)  // could also be made in LIB with SPECIALIZE
{
    INCLUDE_PARAMS_OF_GENERATOR;

    assert(STATE == STATE_0);

    Copy_Cell(ARG(SPEC), EMPTY_BLOCK);
    USED(ARG(BODY));
    return NATIVE_CFUNC(YIELDER)(LEVEL);
}


//
//  /definitional-yield: native [
//
//  "Function used with GENERATOR and YIELDER to emit results"
//
//      return: "Same atom given as input is returned when YIELD resumes"
//          [any-atom?]
//      ^atom "Atom to yield, or the 'done' raised error to signal completion"
//          [any-atom?]
//      :final "Yield, but also signal the yielder or generator is done"
//  ]
//
DECLARE_NATIVE(DEFINITIONAL_YIELD)
//
// 1. It would be possible to give yielders a definitional RETURN, with the
//    meaning "YIELD but then be finished".  e.g.
//
//        yielder [x] [if x = 10 [return x, ~<not run>~] yield 20]
//        =>
//        yielder [x] [if x = 10 [yield x, yield done, ~<not run>~] yield 20]
//
//    But the usefulness of having a slightly shorter way of saying that is
//    limited, compared to having visibility of the RETURN of any enclosing
//    function to use inside of yielders and generators.  So instead YIELD
//
//    Instead YIELD offers a :FINAL refinement, which can be specialized
//    if you really want to.
//
//        yielder [x] [
//            let return: yield:final/
//            if x = 10 [return x, ~<not run>~]
//            yield 20
//        ]
{
    INCLUDE_PARAMS_OF_DEFINITIONAL_YIELD;

    switch (STATE) {
      case ST_YIELD_INITIAL_ENTRY:
        break;

      case ST_YIELD_SUSPENDED:  // see definition for why this state exists
        return OUT;

      default: assert(false);
    }

    Element* meta = Element_ARG(ATOM);

  //=//// EXTRACT YIELDER FROM DEFINITIONAL YIELD'S CELL ///////////////////=//

    Level* yield_level = LEVEL;  // ...make synonyms more obvious

    VarList* yielder_context = maybe Level_Coupling(yield_level);
    if (not yielder_context)
        return FAIL("Must have yielder to jump to");

    Level* yielder_level = Level_Of_Varlist_May_Fail(yielder_context);
    if (not yielder_level)
        return FAIL("Cannot yield to generator that completed or errored");

    if (LEVEL_STATE_BYTE(yielder_level) != ST_YIELDER_RUNNING_BODY)
        return FAIL("YIELD called when body of bound yielder is not running");

    Details* yielder_details = Ensure_Level_Details(yielder_level);
    assert(Details_Dispatcher(yielder_details) == &Yielder_Dispatcher);

    Value* plug = Details_At(yielder_details, IDX_YIELDER_PLUG);
    assert(Not_Cell_Readable(plug));

    Value* meta_yielded = Details_At(yielder_details, IDX_YIELDER_META_YIELDED);
    assert(Not_Cell_Readable(meta_yielded));

  //=//// IF YIELD:FINAL OR RAISED ERROR, THROW YIELD'S ARGUMENT //////////=//

    // If we are doing a YIELD with no intent to resume, then we can just use
    // a conventional BOUNCE_THROWN mechanic, which destroys the stack levels
    // as it climbs up the trampoline.  So that works for either YIELD:FINAL
    // of one value, YIELD DONE, or YIELD of any other raised error which the
    // yielder will promote to an abrupt failure.

    if (Is_Meta_Of_Raised(meta) or REF(FINAL)) {  // not resumable, throw
        Init_Action(
            SPARE,  // use as label for throw
            Cell_Frame_Phase(LIB(DEFINITIONAL_YIELD)),
            CANON(YIELD),
            Level_Varlist(yielder_level)
        );
        return Init_Thrown_With_Label(LEVEL, meta, stable_SPARE);
    }

  //=//// PLAIN YIELD MUST "UNPLUG STACK" FOR LATER RESUMPTION ////////////=//

    // 1. Instead of destroying the stack with a throw, we unplug stack Levels
    //    into a HANDLE! that is a "plug" structure.  Once that plug has been
    //    formed, the Yielder's Level will be back at the top of the stack to
    //    return the yielded value.  Future calls to the Yielder can then put
    //    the Levels back to where this YIELD is at the top again.
    //
    // 2. The way the Trampoline works at the moment, it has the notion of
    //    a Level that was in effect when it called the Executor...and then
    //    even if you rearrange the stack so that Level isn't on the stack
    //    at all any more (as this Yield won't be), it still checks the
    //    Level it called with for its state byte, which can't be STATE_0.
    //    There could be a different BOUNCE_XXX that doesn't check that...

    Unplug_Stack(plug, yielder_level, yield_level);  // preserve stack [1]
    assert(yielder_level == TOP_LEVEL);

    Copy_Cell(meta_yielded, meta);  // atom argument is already ^META

    STATE = ST_YIELD_SUSPENDED;  // can't BOUNCE_CONTINUE with STATE_0 [2]
    return BOUNCE_CONTINUE;  // now continues yielder_level, not yield_level
}
